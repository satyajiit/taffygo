#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Deterministic pseudo-localization of TaffyGo strings.

PAR-L10N-001 is an M1 **Required** row in the browser parity matrix: English
UI, every string externalized, pseudo-localization tested. This module is the
"tested" half. It transforms a source string the same two ways Android and
grit do, and — more usefully — it is invertible, so a check can prove the
transform touched only what it was supposed to touch.

Two transforms, matching the two pseudo-locales Android defines:

  * **accented** (`en-XA`): every Latin letter becomes an accented lookalike,
    the text is bracketed, and it is padded to the expansion ratio Android
    uses for its own pseudo-locale. It finds three classes of defect at once —
    a string that was never externalized (it stays unaccented on screen), a
    layout that cannot survive a longer translation, and a character the font
    cannot render.
  * **bidi** (`ar-XB`): the text is wrapped in explicit right-to-left
    override marks. It finds layouts that hard-code a left-to-right reading
    order.

Placeholders are never transformed. A placeholder that came out accented would
be a placeholder the runtime can no longer substitute, so `placeholders()`
below is the definition of what is left alone, and the check asserts it.

Stdlib only. Importable as a module; not executable on its own.
"""

from __future__ import annotations

import math
import re

# The Latin letters, and what each becomes in the accented pseudo-locale. The
# map is total over ASCII letters on purpose: a letter with no entry would be
# passed through unchanged, and an unaccented letter in the output is exactly
# the signal the transform exists to produce, so a gap here would be a false
# negative rather than a visible bug.
ACCENT_MAP = {
    "a": "å", "b": "ƀ", "c": "ç", "d": "ð", "e": "é",
    "f": "ƒ", "g": "ğ", "h": "ĥ", "i": "î", "j": "ĵ",
    "k": "ķ", "l": "ļ", "m": "ḿ", "n": "ñ", "o": "ö",
    "p": "þ", "q": "ǫ", "r": "ŕ", "s": "š", "t": "ţ",
    "u": "û", "v": "ṽ", "w": "ŵ", "x": "ẋ", "y": "ý",
    "z": "ž",
    "A": "Å", "B": "Ɓ", "C": "Ç", "D": "Ð", "E": "É",
    "F": "Ƒ", "G": "Ğ", "H": "Ĥ", "I": "Î", "J": "Ĵ",
    "K": "Ķ", "L": "Ļ", "M": "Ḿ", "N": "Ñ", "O": "Ö",
    "P": "Þ", "Q": "Ǫ", "R": "Ŕ", "S": "Š", "T": "Ţ",
    "U": "Û", "V": "Ṽ", "W": "Ŵ", "X": "Ẋ", "Y": "Ý",
    "Z": "Ž",
}

# The inverse, built once so the round-trip assertion in the check is cheap.
UNACCENT_MAP = {accented: plain for plain, accented in ACCENT_MAP.items()}

ACCENT_OPEN = "["
ACCENT_CLOSE = "]"

# The padding alphabet. Deliberately made of accented characters that are not
# in ACCENT_MAP's value set, so padding can never be mistaken for transformed
# source text when the round-trip strips it.
PAD_ALPHABET = "¡¿«»"

# Explicit bidirectional formatting characters, from Unicode Annex #9.
RLO = "‮"  # right-to-left override
PDF = "‬"  # pop directional formatting

# Expansion ratios by source length, matching Android's own pseudo-locale
# behaviour: short strings grow proportionally more, because a label of three
# characters is where a fixed-width layout breaks first. These are the
# definition of the transform, not a product target.
_EXPANSION_BUCKETS = ((10, 2.0), (20, 1.8), (30, 1.6), (50, 1.4))
_EXPANSION_DEFAULT = 1.3

# What must survive a transform untouched. Three shapes appear in this
# repository's catalogues, and a fourth would be a new decision rather than a
# new regular expression:
#   grit placeholders            <ph name="COUNT">$1<ex>3</ex></ph>
#   Android format specifiers    %s  %1$s  %2$d
#   Android positional arguments $1  $2
PLACEHOLDER = re.compile(
    r"<ph\b[^>]*>.*?</ph>"
    r"|%\d+\$[a-zA-Z]"
    r"|%[a-zA-Z]"
    r"|\$\d+"
)


class PseudolocaleError(Exception):
    """A string this module refuses to transform."""


def placeholders(text: str) -> list[str]:
    """Every substring the transforms must leave exactly as it found it."""
    return [match.group(0) for match in PLACEHOLDER.finditer(text)]


def _split_on_placeholders(text: str) -> list[tuple[bool, str]]:
    """[(is_placeholder, chunk), ...] covering `text` exactly once."""
    chunks: list[tuple[bool, str]] = []
    position = 0
    for match in PLACEHOLDER.finditer(text):
        if match.start() > position:
            chunks.append((False, text[position : match.start()]))
        chunks.append((True, match.group(0)))
        position = match.end()
    if position < len(text):
        chunks.append((False, text[position:]))
    return chunks


def expansion_ratio(length: int) -> float:
    for limit, ratio in _EXPANSION_BUCKETS:
        if length < limit:
            return ratio
    return _EXPANSION_DEFAULT


def _padding(source_length: int, translated_length: int) -> str:
    """Deterministic filler that brings the output up to the expansion ratio."""
    target = math.ceil(source_length * expansion_ratio(source_length))
    missing = target - translated_length
    if missing <= 0:
        return ""
    # A leading space so the padding reads as a separate word rather than as a
    # corrupted final word of the message.
    body = "".join(PAD_ALPHABET[index % len(PAD_ALPHABET)] for index in range(missing - 1))
    return " " + body if missing > 1 else " "


def accented(text: str) -> str:
    """The `en-XA` transform: accent, bracket and expand."""
    out: list[str] = []
    for is_placeholder, chunk in _split_on_placeholders(text):
        out.append(chunk if is_placeholder else "".join(ACCENT_MAP.get(c, c) for c in chunk))
    body = "".join(out)
    return f"{ACCENT_OPEN}{body}{_padding(len(text), len(body))}{ACCENT_CLOSE}"


def strip_accented(text: str) -> str:
    """Invert `accented`. Raises if the input is not a well-formed output."""
    if not (text.startswith(ACCENT_OPEN) and text.endswith(ACCENT_CLOSE)):
        raise PseudolocaleError("accented text is not bracketed")
    body = text[len(ACCENT_OPEN) : -len(ACCENT_CLOSE)]
    padding_start = len(body)
    while padding_start > 0 and body[padding_start - 1] in PAD_ALPHABET:
        padding_start -= 1
    if padding_start > 0 and padding_start < len(body) and body[padding_start - 1] == " ":
        padding_start -= 1
    elif padding_start == len(body) and body.endswith(" "):
        padding_start -= 1
    body = body[:padding_start]
    out: list[str] = []
    for is_placeholder, chunk in _split_on_placeholders(body):
        out.append(chunk if is_placeholder else "".join(UNACCENT_MAP.get(c, c) for c in chunk))
    return "".join(out)


def bidi(text: str) -> str:
    """The `ar-XB` transform: force a right-to-left reading order."""
    out: list[str] = []
    for is_placeholder, chunk in _split_on_placeholders(text):
        out.append(chunk if is_placeholder else f"{RLO}{chunk}{PDF}")
    return "".join(out)


def strip_bidi(text: str) -> str:
    """Invert `bidi`."""
    return text.replace(RLO, "").replace(PDF, "")


def unmapped_letters(text: str) -> set[str]:
    """Latin letters outside placeholders that the accent map does not cover."""
    missing: set[str] = set()
    for is_placeholder, chunk in _split_on_placeholders(text):
        if is_placeholder:
            continue
        for character in chunk:
            if character.isascii() and character.isalpha() and character not in ACCENT_MAP:
                missing.add(character)
    return missing
