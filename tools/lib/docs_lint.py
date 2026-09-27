#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Documentation consistency checks for ./tools/check docs.

Makes the manual review rules in CONTRIBUTING.md executable:

  1. banned user-facing vocabulary (voice-and-naming.md section 4)
  2. status labels: only the four defined in docs/README.md, and every
     [Open ...] cites a register entry
  3. every OD-nnn reference is defined in docs/open-decisions.md
  4. every decision record is listed in docs/decisions/README.md
  5. every relative Markdown link resolves, including its #anchor

Host Python 3 only. It is already a hard prerequisite of depot_tools and
gclient, and is unrelated to decision 0007's sandboxed Python utility service,
which stays deferred to M7.

Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import fnmatch
import os
import re
import sys

# --- what gets scanned ------------------------------------------------------

# This linter reviews documentation the repository *authors*. Two kinds of
# directory are therefore skipped, and the distinction matters when adding one:
#
#   - not ours to review: .git, .taffy (machine-local state), _refs (untracked
#     reference checkouts), node_modules, and docs/archive (superseded
#     originals, kept for intent and explicitly not authority);
#   - generated: every build tree. A generator that copies a Markdown file into
#     an output directory moves it relative to the links inside it, so linting
#     the copy reports broken links that do not exist in the source. Skipping
#     the tree fixes the report; "fixing" the copied links would corrupt the
#     source. Every name here is also gitignored — nothing skipped is committed.
SKIP_DIRS = {
    ".git",
    ".taffy",
    "_refs",
    "node_modules",
    "docs/archive",
    # Local HyperFrames compositions. They are not product documentation; the
    # generated half is gitignored, and the source half is not in SOURCE_ROOTS.
    "videos-harness",
    # Retired pre-cutover harness. Product UI lives under taffy-core/ui/android.
    "apps",
    # Knowledge-graph tool output. Its report quotes source and prose from the
    # tree it describes, so linting it reports this repository's vocabulary
    # rules against text this repository did not write — and the finding cannot
    # be fixed, because the file is regenerated.
    "graphify-out",
    # Agent worktrees: whole checkouts of other revisions, nested in this one
    # and excluded through .git/info/exclude. Linting them reports another
    # revision's findings against this tree — 920 of them on 2026-09-26, from
    # ten worktrees whose branches predate the rules they were read against.
    ".claude/worktrees",
    # Checksum-verified upstream source mounted by Chromium sync. Its Markdown
    # is CPython documentation, not TaffyGo-authored product vocabulary.
    "taffy-core/third_party/cpython/src",
    # generated trees
    "build",
    "out",
    "target",
    "dist",
    ".gradle",
    ".kotlin",
    ".next",
    "__pycache__",
}

BANNED_RULES = [
    (
        "release-vocabulary",
        re.compile(
            r"\balpha\b|\bbeta\b|\bMVP\b|\brollout\b"
            r"|Production (foundation|launch|expansion)|\[Future\]",
            re.IGNORECASE,
        ),
        "One release: TaffyGo 1.0. Use a milestone (M0-M8) or a spike (SP-nn).",
    ),
    (
        "retired-names",
        re.compile(r"AI Notch|Intent Box|\bSoul\b|\bEA\b|User Drives|AI Drives|Take Control"),
        "Use the plain-language name from docs/voice-and-naming.md.",
    ),
    (
        "retired-structure",
        re.compile(r"\bG[0-8]\b|\bV[1-7]\b|\bR[1-4]\b|multi-agent|custom agent"),
        "Milestones M0-M8 and spikes SP-nn only; one assistant (decision 0009).",
    ),
]

BAD_LABELS = re.compile(r"\[(Future|TODO|TBD|WIP|Draft|Planned|Someday|Deprecated)\](?!\()")
# The label itself, not a Markdown link like [Open decisions](open-decisions.md).
OPEN_LABEL = re.compile(r"\[Open[^\]]*\](?!\()")
LABEL_INVENTORY = re.compile(r"\[Current\]")
# A citation may be the register id, the documented placeholder, or a link to
# the register itself, and Markdown wraps it onto the next line often enough
# that the whole sentence has to be considered.
OD_CITATION = re.compile(r"\bOD-(\d{3}|nnn)\b|\bOD id\b|open-decisions\.md")
CITATION_WINDOW = 3
OD_REFERENCE = re.compile(r"\bOD-(\d{3})\b")
OD_DEFINITION = re.compile(r"^\|\s*OD-(\d{3})\s*\|")
# Matched over a whole file, because Markdown wraps link text: `[verification
# report](...)` is one link over two lines. The text may hold single line
# breaks but no blank line, which ends the paragraph and so the link.
LINK = re.compile(r"\[(?:[^\[\]\n]|\n(?![ \t]*\n))*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")


def slugify(heading: str) -> str:
    """GitHub-compatible heading anchor."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links -> label
    text = text.replace("`", "").replace("*", "")
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    return re.sub(r"\s", "-", text.strip())


class Findings:
    def __init__(self) -> None:
        self.items: list[tuple[str, int, str, str]] = []

    def add(self, path: str, line: int, rule: str, message: str) -> None:
        self.items.append((path, line, rule, message))

    def __len__(self) -> int:
        return len(self.items)


def load_allowlist(root: str) -> list[tuple[str, re.Pattern[str], str]]:
    path = os.path.join(root, "tools", "check.d", "allow-terms.tsv")
    rules: list[tuple[str, re.Pattern[str], str]] = []
    if not os.path.exists(path):
        return rules
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            rules.append((parts[0], re.compile(parts[1], re.IGNORECASE), parts[2]))
    return rules


def allowed(rules, rel_path: str, line: str) -> bool:
    for glob, pattern, _reason in rules:
        if fnmatch.fnmatch(rel_path, glob) and pattern.search(line):
            return True
    return False


def markdown_files(root: str) -> list[str]:
    found: list[str] = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        rel_dir = "" if rel_dir == "." else rel_dir
        dirnames[:] = [
            d
            for d in dirnames
            if d not in SKIP_DIRS
            and os.path.join(rel_dir, d).replace(os.sep, "/") not in SKIP_DIRS
        ]
        for name in filenames:
            if name.endswith(".md"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def read_lines(path: str) -> list[str]:
    with open(path, encoding="utf-8") as handle:
        return handle.read().splitlines()


def code_fence_mask(lines: list[str]) -> list[bool]:
    """True where a line sits inside a fenced code block."""
    inside = False
    mask = []
    for line in lines:
        if FENCE.match(line):
            inside = not inside
            mask.append(True)
        else:
            mask.append(inside)
    return mask


def anchors_of(path: str) -> set[str]:
    anchors: set[str] = set()
    try:
        lines = read_lines(path)
    except (OSError, UnicodeDecodeError):
        return anchors
    seen: dict[str, int] = {}
    for line, in_code in zip(lines, code_fence_mask(lines)):
        if in_code:
            continue
        match = HEADING.match(line)
        if not match:
            continue
        slug = slugify(match.group(2))
        if slug in seen:
            seen[slug] += 1
            anchors.add(f"{slug}-{seen[slug]}")
        else:
            seen[slug] = 0
            anchors.add(slug)
    return anchors


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    root = os.path.abspath(root)
    findings = Findings()
    allowlist = load_allowlist(root)
    files = markdown_files(root)

    def rel(path: str) -> str:
        return os.path.relpath(path, root).replace(os.sep, "/")

    # --- OD register ---------------------------------------------------------
    od_defined: set[str] = set()
    od_register = os.path.join(root, "docs", "open-decisions.md")
    if os.path.exists(od_register):
        for line in read_lines(od_register):
            match = OD_DEFINITION.match(line)
            if match:
                od_defined.add(match.group(1))

    # --- decision index ------------------------------------------------------
    decisions_dir = os.path.join(root, "docs", "decisions")
    index_path = os.path.join(decisions_dir, "README.md")
    if os.path.isdir(decisions_dir) and os.path.exists(index_path):
        index_text = "\n".join(read_lines(index_path))
        for name in sorted(os.listdir(decisions_dir)):
            if not re.match(r"^\d{4}-.*\.md$", name):
                continue
            if name not in index_text:
                findings.add(
                    rel(index_path), 0, "decision-index",
                    f"{name} exists but is not listed in the decision log",
                )

    # --- per-file rules ------------------------------------------------------
    for path in files:
        rel_path = rel(path)
        lines = read_lines(path)
        mask = code_fence_mask(lines)

        for number, (line, in_code) in enumerate(zip(lines, mask), start=1):
            if not in_code:
                for rule, pattern, hint in BANNED_RULES:
                    match = pattern.search(line)
                    if match and not allowed(allowlist, rel_path, line):
                        findings.add(
                            rel_path, number, rule,
                            f'"{match.group(0)}" is banned vocabulary. {hint}',
                        )

                if not allowed(allowlist, rel_path, line):
                    for match in BAD_LABELS.finditer(line):
                        findings.add(
                            rel_path, number, "status-label",
                            f"{match.group(0)} is not a status label. "
                            "Use [Current], [Decided], [Proposed], or [Open (OD-nnn)].",
                        )
                    # An [Open] label must cite its register entry. The citation
                    # is written either inside the label or beside it on the same
                    # line; a line inventorying every label is not a claim.
                    sentence = " ".join(lines[number - 1 : number - 1 + CITATION_WINDOW])
                    inventory = LABEL_INVENTORY.search(
                        " ".join(lines[max(0, number - CITATION_WINDOW) : number])
                    )
                    if not inventory and not OD_CITATION.search(sentence):
                        for match in OPEN_LABEL.finditer(line):
                            findings.add(
                                rel_path, number, "status-label",
                                f"{match.group(0)} does not cite a register entry (OD-nnn).",
                            )

            for match in OD_REFERENCE.finditer(line):
                if od_defined and match.group(1) not in od_defined:
                    findings.add(
                        rel_path, number, "open-decision",
                        f"OD-{match.group(1)} is not defined in docs/open-decisions.md",
                    )

        # Over the whole file, so a link whose text wraps is still a link; it
        # is reported at the line where its text opens.
        text = "\n".join(lines)
        for match in LINK.finditer(text):
            number = text.count("\n", 0, match.start()) + 1
            target = match.group(1)
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
                continue  # http, https, mailto, ...
            anchor = ""
            if "#" in target:
                target, anchor = target.split("#", 1)
            resolved = path if target == "" else os.path.normpath(
                os.path.join(os.path.dirname(path), target)
            )
            if not os.path.exists(resolved):
                findings.add(
                    rel_path, number, "broken-link",
                    f"{match.group(1)} does not resolve",
                )
                continue
            if anchor and resolved.endswith(".md"):
                if anchor.lower() not in anchors_of(resolved):
                    findings.add(
                        rel_path, number, "broken-anchor",
                        f"#{anchor} is not a heading in {rel(resolved)}",
                    )

    for path, number, rule, message in findings.items:
        location = f"{path}:{number}" if number else path
        print(f"{location}: {rule}: {message}")

    print()
    print(f"checked {len(files)} Markdown files, {len(findings)} finding(s)")
    return 1 if len(findings) else 0


if __name__ == "__main__":
    sys.exit(main())
