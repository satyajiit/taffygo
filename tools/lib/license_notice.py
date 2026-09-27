#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The per-file licence notice: its text, where it goes, and how to write it.

TaffyGo's own source is published under the Mozilla Public License, version
2.0 (decision 0205). The MPL is a file-level licence, so the file is where it
has to say so: Exhibit A of the licence gives the wording, and a file carrying
it stays licensed wherever it travels, which a root LICENSE alone cannot do.

This module owns the mechanical half of that -- the notice text, one comment
style per file type, and where in a file the notice may go. It lives apart
from ``license_headers.py`` because that file is the gate and this one is the
rule it enforces: the gate reads a tree and reports, this rewrites a string
and has no opinion about which files matter.

Placement is the part worth being careful about, because a notice in the wrong
place is worse than none -- it compiles and it lies. Two prologues must stay
first: a ``#!`` line, which the kernel reads at byte zero, and an XML
declaration, which is only a declaration at the start of the document. The
notice follows those and precedes everything else.

Stdlib only. Pure functions over text; nothing here touches the filesystem.
"""

from __future__ import annotations

import os
import re

#: The holder. Ownership did not change when the licence did; publishing is a
#: grant rather than a transfer, which is why the copyright line survives the
#: relicensing with only "All rights reserved." removed from it.
COPYRIGHT = "Copyright (c) 2026 Matterward Labs Private Limited."

#: Exhibit A, wrapped. The wording is the licence's own and is not editable;
#: only the line breaks are ours.
EXHIBIT_A = (
    "This Source Code Form is subject to the terms of the Mozilla Public",
    "License, v. 2.0. If a copy of the MPL was not distributed with this",
    "file, You can obtain one at https://mozilla.org/MPL/2.0/.",
)

#: The sentence a file must carry, with every comment marker and line break
#: taken out of it, so one shape of notice can be recognised in all of them.
_CANONICAL = " ".join(EXHIBIT_A)

#: The notice this relicensing replaces. It is matched rather than assumed:
#: a file carrying it says the opposite of the root LICENSE.
LEGACY = re.compile(
    r"Copyright \(c\) \d{4} Matterward Labs Private Limited\. All rights reserved\."
)


class Style:
    """One comment syntax, and how a five-line notice is written in it."""

    def __init__(self, opener: str, closer: str | None = None) -> None:
        self.opener = opener
        self.closer = closer

    def render(self) -> str:
        lines = [COPYRIGHT, "", *EXHIBIT_A]
        if self.closer is None:
            body = "\n".join(
                (self.opener if not line else f"{self.opener} {line}") for line in lines
            )
            return body + "\n"
        inner = "\n".join(lines)
        return f"{self.opener}\n{inner}\n{self.closer}\n"


LINE_SLASH = Style("//")
LINE_HASH = Style("#")
BLOCK_XML = Style("<!--", "-->")
BLOCK_C = Style("/*", "*/")
BLOCK_JINJA = Style("{#", "#}")

#: Which file types carry a notice, and in whose comment syntax. An extension
#: absent from this map is not a file this rule speaks about: a JSON document
#: has no comment syntax, a fixture's bytes are the fixture, and a patch is a
#: diff of somebody else's source.
STYLES: dict[str, Style] = {
    ".cc": LINE_SLASH, ".cpp": LINE_SLASH, ".h": LINE_SLASH, ".hpp": LINE_SLASH,
    ".mm": LINE_SLASH, ".rs": LINE_SLASH, ".kt": LINE_SLASH, ".kts": LINE_SLASH,
    ".java": LINE_SLASH, ".mojom": LINE_SLASH, ".ts": LINE_SLASH, ".tsx": LINE_SLASH,
    ".js": LINE_SLASH, ".mjs": LINE_SLASH, ".cjs": LINE_SLASH,
    ".py": LINE_HASH, ".sh": LINE_HASH, ".bash": LINE_HASH,
    ".gn": LINE_HASH, ".gni": LINE_HASH, ".pro": LINE_HASH,
    ".xml": BLOCK_XML,
    ".css": BLOCK_C, ".scss": BLOCK_C,
    ".jinja2": BLOCK_JINJA,
}

#: A comment opener that has to be closed, and the marker that closes it. A
#: prefix ending in one of these is not a line marker, and the difference is
#: the difference between a notice and an unparseable file.
_BLOCK_OPENER = re.compile(r"(<!--|/\*|\{#)\s*$")
_BLOCK_CLOSERS = {"<!--": "-->", "/*": "*/", "{#": "#}"}

#: A shebang, and specifically not Rust's `#![...]` inner attribute, which
#: begins with the same two characters. Reading one as the other puts the
#: notice on line two of every Rust file that opens with a crate attribute,
#: below a line that is code.
_SHEBANG = re.compile(r"^#!(?!\[)")
_XML_DECLARATION = re.compile(r"^\s*<\?xml\b[^>]*\?>")


def style_for(relative: str, first_line: str = "") -> Style | None:
    """The comment syntax for a path, or None when it carries no notice.

    A file with no extension is a script when it says so on its first line,
    and otherwise is left alone: ``DEPS`` and ``OWNERS`` are Chromium's
    configuration vocabulary rather than source code, and a notice is not
    required in them -- though one that contradicts the licence is still
    rejected, by the gate rather than here.
    """
    _, extension = os.path.splitext(relative)
    if extension in STYLES:
        return STYLES[extension]
    if not extension and _SHEBANG.match(first_line):
        return LINE_HASH
    return None


#: A comment marker is only a marker at the edge of a line. Stripping the
#: sequence anywhere would take the "//" out of the licence's own URL and
#: stop the notice matching itself, which is exactly what it did once.
_OPENER = re.compile(r"^[ \t]*(///?|#+|<!--|\{#|/\*|\*)[ \t]?")
_CLOSER = re.compile(r"[ \t]*(-->|#\}|\*/)[ \t]*$")


#: How far into a file the notice may sit and still be the file's notice.
#: Bounded on purpose. Sixteen generators in this tree emit the notice inside
#: a string literal, and an unbounded search reads those literals as the
#: generator's own header -- so the one kind of file most likely to lose its
#: header silently would be the one kind exempt from the rule.
#:
#: Eight, because that is where the real ones are and nowhere near where the
#: emitted ones are. Measured over the whole tree: 4,965 notices begin on line
#: one, 198 on line two, 70 on line three and one on line four -- an XML
#: declaration, a blank and a comment opener ahead of it. The gap to the next
#: occurrence is the point. A window of twenty swallowed a small generator
#: whose emitted literal began on line twelve, and read it as that file's own
#: header; the file had none at all.
HEAD_LINES = 8


def carries_notice(text: str) -> bool:
    """True when Exhibit A heads the file, in any comment syntax."""
    flattened: list[str] = []
    for line in text.splitlines()[:HEAD_LINES]:
        line = _CLOSER.sub("", line)
        line = _OPENER.sub("", line)
        flattened.append(line)
    return _CANONICAL in " ".join(" ".join(flattened).split())


def carries_legacy(text: str) -> bool:
    """True when the superseded all-rights-reserved line is present."""
    return LEGACY.search(text) is not None


def split_prologue(text: str) -> tuple[str, str]:
    """Split the lines that must stay first from the rest of the file."""
    prologue: list[str] = []
    rest = text.splitlines(keepends=True)
    if rest and _SHEBANG.match(rest[0]):
        prologue.append(rest.pop(0))
    if rest and _XML_DECLARATION.match(rest[0]):
        prologue.append(rest.pop(0))
    return "".join(prologue), "".join(rest)


#: The superseded notice as it sits on its own line, with whatever comes
#: before and after it on that line. The prefix is the comment marker the
#: file already uses -- "// ", "# ", "<!-- " or nothing at all inside a block
#: comment -- and reusing it is what lets the new notice take the old one's
#: place rather than being stacked on top of it.
_LEGACY_LINE = re.compile(
    r"^(?P<prefix>[^\n]*?)"
    r"Copyright \(c\) \d{4} Matterward Labs Private Limited\. All rights reserved\."
    r"(?P<suffix>[^\n]*)$",
    re.MULTILINE,
)


def replace_legacy(text: str) -> str | None:
    """Swap the superseded notice for this one, where it already sits.

    Returns None when there is nothing to swap. Substituting in place matters
    more than it sounds: most of these lines are the first line of an existing
    comment block that goes on to say what the file is, and prepending a
    second block would leave that one headless and the file with two.
    """
    head = "\n".join(text.splitlines()[:HEAD_LINES])
    match = _LEGACY_LINE.search(head)
    if match is None:
        return None
    prefix, suffix = match.group("prefix"), match.group("suffix")
    opener = _BLOCK_OPENER.search(prefix)
    if opener is not None and _BLOCK_CLOSERS[opener.group(1)] in suffix:
        # The whole notice was one closed block comment -- `<!-- ... -->` on a
        # single line. Its opener is not a line marker and repeating it opens
        # four more comments that nothing closes, which is a file that no
        # longer parses rather than a file that reads oddly. It becomes one
        # block instead, five lines between the same two markers.
        indent = prefix[: opener.start(1)]
        closer = _BLOCK_CLOSERS[opener.group(1)]
        lines = [f"{indent}{opener.group(1)}", COPYRIGHT, "", *EXHIBIT_A, f"{indent}{closer}"]
    else:
        lines = [f"{prefix}{COPYRIGHT}", prefix.rstrip()]
        lines += [f"{prefix}{part}" for part in EXHIBIT_A]
        lines[-1] += suffix
    return text[: match.start()] + "\n".join(lines) + text[match.end():]


def apply(text: str, style: Style, replace: bool = True) -> str:
    """Return ``text`` carrying exactly one notice, in ``style``.

    ``replace`` false prepends without consuming a superseded notice, which
    is what a file wants when the old wording is not its header but a string
    it emits into something else.

    Idempotent: a file that already carries Exhibit A is returned unchanged,
    whatever else is in it, because re-running a rewrite over a tree is how a
    tree ends up with two of something.
    """
    if carries_notice(text):
        return text
    if replace:
        replaced = replace_legacy(text)
        if replaced is not None:
            return replaced
    prologue, body = split_prologue(text)
    body = body.lstrip("\n")
    notice = style.render()
    if body:
        notice += "\n"
    return prologue + notice + body


def self_test() -> list[str]:
    """Prove each syntax, both prologues, idempotence and legacy removal."""
    failures: list[str] = []

    rendered = LINE_SLASH.render()
    if not rendered.startswith("// Copyright (c) 2026") or "\n//\n" not in rendered:
        failures.append("the line-comment notice lost its blank separator")
    if not carries_notice(rendered):
        failures.append("a rendered line-comment notice is not recognised as one")
    if not carries_notice(BLOCK_XML.render()):
        failures.append("a rendered XML notice is not recognised as one")
    if not carries_notice(BLOCK_JINJA.render()):
        failures.append("a rendered Jinja notice is not recognised as one")
    if carries_notice("// Copyright (c) 2026 Matterward Labs Private Limited.\n"):
        failures.append("a bare copyright line counted as the licence notice")
    buried = "import os\n" * HEAD_LINES + 'HEADER = """' + LINE_SLASH.render() + '"""\n'
    if carries_notice(buried):
        failures.append("a notice a generator emits counted as that generator's own")
    repaired = apply(buried, LINE_HASH)
    if not repaired.startswith("# Copyright"):
        failures.append("a generator that emits the notice was not given one of its own")

    script = "#!/usr/bin/env python3\nimport os\n"
    fixed = apply(script, LINE_HASH)
    if not fixed.startswith("#!/usr/bin/env python3\n# Copyright"):
        failures.append("the notice displaced a shebang from the first line")
    if not fixed.rstrip().endswith("import os"):
        failures.append("applying the notice lost the file body")

    document = '<?xml version="1.0" encoding="utf-8"?>\n<resources />\n'
    fixed = apply(document, BLOCK_XML)
    if not fixed.startswith('<?xml version="1.0" encoding="utf-8"?>\n<!--'):
        failures.append("the notice displaced an XML declaration")

    # Assembled rather than spelled out: this module is inside the scan that
    # reads it, and a fixture written as one literal makes the file carrying
    # the rule the one file that breaks it.
    legacy = (
        f"// {COPYRIGHT[:-1]}. All rights " + "reserved.\n"
        "\n"
        "#include \"taffy/browser/thing.h\"\n"
    )
    fixed = apply(legacy, LINE_SLASH)
    if carries_legacy(fixed):
        failures.append("the superseded notice survived the rewrite")
    if not carries_notice(fixed):
        failures.append("the rewrite did not leave a licence notice behind")
    if fixed.count("#include") != 1 or "\n\n#include" not in fixed:
        failures.append("the rewrite did not keep the file's own spacing")
    if not fixed.startswith("// Copyright"):
        failures.append("the replacement did not take the old notice's place")

    one_liner = (
        '<?xml version="1.0"?>\n'
        + f"<!-- {COPYRIGHT[:-1]}. All rights " + "reserved. -->\n"
        + "<resources />\n"
    )
    fixed_one_liner = apply(one_liner, BLOCK_XML)
    if fixed_one_liner.count("<!--") != 1 or fixed_one_liner.count("-->") != 1:
        failures.append("a one-line block comment was expanded into unclosed openers")
    if "<resources />" not in fixed_one_liner:
        failures.append("expanding a one-line block comment lost the document")
    import xml.etree.ElementTree as _ET
    try:
        _ET.fromstring(fixed_one_liner.split("?>", 1)[1])
    except Exception:  # noqa: BLE001 - any parse error is the finding
        failures.append("the rewritten document no longer parses as XML")

    inside_block = (
        '<?xml version="1.0"?>\n<!--\n'
        + f"{COPYRIGHT[:-1]}. All rights " + "reserved.\n"
        + "\nWhat this file is.\n-->\n<resources />\n"
    )
    fixed_block = apply(inside_block, BLOCK_XML)
    if fixed_block.count("<!--") != 1:
        failures.append("a notice inside a comment block gained a second block")
    if "What this file is." not in fixed_block:
        failures.append("replacing a notice inside a block dropped the rest of it")
    if apply(fixed, LINE_SLASH) != fixed:
        failures.append("applying the notice twice changed the file")

    empty = apply("", LINE_SLASH)
    if empty != LINE_SLASH.render():
        failures.append("an empty file gained a trailing blank line it did not need")

    attributed = apply("#![allow(dead_code)]\n\nuse std::fmt;\n", LINE_SLASH)
    if not attributed.startswith("// Copyright"):
        failures.append("a Rust inner attribute was mistaken for a shebang")
    if "#![allow(dead_code)]" not in attributed:
        failures.append("applying the notice lost a crate attribute")

    emitted = (
        "#!/usr/bin/env python3\n"
        '"""What this generator is."""\n\n'
        "def render():\n"
        "    return  + LINE_SLASH.render() + \n"
    )
    if carries_notice(emitted):
        failures.append("a notice a generator emits near its top counted as its own")

    if style_for("a/b/thing.cc") is not LINE_SLASH:
        failures.append("a C++ source file resolved to the wrong comment syntax")
    if style_for("a/b/OWNERS") is not None:
        failures.append("a configuration file with no extension was required to carry one")
    if style_for("tools/check", "#!/usr/bin/env bash\n") is not LINE_HASH:
        failures.append("an extensionless shell script was not recognised by its shebang")
    if style_for("a/b/manifest.json") is not None:
        failures.append("a document with no comment syntax was given one")

    return failures


if __name__ == "__main__":
    import sys

    problems = self_test()
    for problem in problems:
        print(f"licence notice self-test: {problem}", file=sys.stderr)
    if problems:
        sys.exit(1)
    print("licence notice self-test: all fixtures passed")
