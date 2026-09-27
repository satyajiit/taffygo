#!/usr/bin/env python3
"""Self-check for the TaffyGo web fixture corpus.

Verifies four invariants that keep the corpus honest and its version meaningful:

  1. every fixture and asset named in manifest.json exists on disk;
  2. every content file on disk is named in the manifest (no orphan pages, no
     drift that would silently change what a benchmark measures);
  3. no page reaches an external network: no absolute http(s) URL, no
     protocol-relative URL, no CDN, no analytics, no remote font/image;
  4. every declared canary token appears only in the fixtures that are supposed
     to carry it, and every carried_by fixture actually contains its token.

Stdlib only, read-only. Exit 0 clean, 1 on any finding.
Run: python3 test-fixtures/web/check.py
"""

from __future__ import annotations

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORIGINS_ROOT = os.path.join(HERE, "origins")
SHARED_ROOT = os.path.join(HERE, "shared")
MANIFEST = os.path.join(HERE, "manifest.json")

# Files that are corpus infrastructure, not served fixture content.
INFRA = {"serve.py", "check.py", "manifest.json", "README.md"}

# Content that is served to a browser and must therefore be in the manifest.
CONTENT_EXT = {".html", ".css", ".js", ".json", ".csv", ".svg", ".wav", ".pdf", ".txt", ".png", ".jpg", ".gif"}

# External-reference smells. Fixtures must be self-contained.
EXTERNAL_PATTERNS = [
    ("absolute-url", re.compile(r"""\b(?:href|src|action|data|poster|content|url)\s*=?\s*["'(]?\s*https?://""", re.IGNORECASE)),
    ("bare-http", re.compile(r"""["'(]https?://""", re.IGNORECASE)),
    ("protocol-relative", re.compile(r"""["'(]//[a-z0-9]""", re.IGNORECASE)),
    ("css-import-remote", re.compile(r"""@import\s+["']?https?://""", re.IGNORECASE)),
    ("srcset-remote", re.compile(r"""srcset\s*=\s*["'][^"']*https?://""", re.IGNORECASE)),
]

# URLs that are legitimately mentioned as data, not fetched. These are vocabulary
# tokens (schema.org type URLs in JSON-LD, and the deliberately refused outside
# host in the redirect fixture), never a resource the page loads.
EXTERNAL_ALLOW = re.compile(
    # XML/SVG namespace URIs are identifiers the parser matches on, never
    # fetched. schema.org type URLs live inside JSON-LD as vocabulary. The
    # exfil.invalid host and the attacker mailbox are the deliberately refused
    # bait in the redirect and injection fixtures, present as data to be ignored.
    r"http://www\.w3\.org/"
    r"|https?://schema\.org"
    r"|https?://exfil\.invalid"
    r"|attacker@hostile\.taffy\.test"
)


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []

    def fail(self, message: str) -> None:
        self.errors.append(message)

    def ok(self) -> bool:
        return not self.errors


def rel(path: str) -> str:
    return os.path.relpath(path, HERE).replace(os.sep, "/")


def disk_content_files() -> set[str]:
    found: set[str] = set()
    for root in (ORIGINS_ROOT, SHARED_ROOT):
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
            for name in filenames:
                if name.startswith("."):
                    continue
                ext = os.path.splitext(name)[1].lower()
                if ext in CONTENT_EXT:
                    found.add(rel(os.path.join(dirpath, name)))
    return found


def scan_external(report: Report, files: set[str]) -> None:
    text_ext = {".html", ".css", ".js", ".svg"}
    for relpath in sorted(files):
        if os.path.splitext(relpath)[1].lower() not in text_ext:
            continue
        full = os.path.join(HERE, relpath)
        with open(full, encoding="utf-8", errors="replace") as handle:
            for number, line in enumerate(handle, start=1):
                stripped = EXTERNAL_ALLOW.sub("", line)
                for label, pattern in EXTERNAL_PATTERNS:
                    if pattern.search(stripped):
                        report.fail(
                            "%s:%d external reference (%s): %s"
                            % (relpath, number, label, line.strip()[:100])
                        )


def scan_canaries(report: Report, manifest: dict, files: set[str]) -> None:
    id_to_path = {f["id"]: f["path"] for f in manifest["fixtures"]}
    # Map every content file to the tokens it contains.
    text_ext = {".html", ".css", ".js", ".svg", ".csv", ".txt"}
    for canary in manifest["canaries"]:
        token = canary["token"]
        expected = set(canary["carried_by"])
        for fid in expected:
            if fid not in id_to_path:
                report.fail("canary %s lists unknown fixture id %r" % (token, fid))
        expected_paths = {id_to_path[fid] for fid in expected if fid in id_to_path}
        actual_paths = set()
        for relpath in files:
            if os.path.splitext(relpath)[1].lower() not in text_ext:
                continue
            with open(os.path.join(HERE, relpath), encoding="utf-8", errors="replace") as handle:
                if token in handle.read():
                    actual_paths.add(relpath)
        leaked = actual_paths - expected_paths
        missing = expected_paths - actual_paths
        for path in sorted(leaked):
            report.fail("canary %s leaked into %s (only %s may carry it)"
                        % (token, path, ", ".join(sorted(expected_paths)) or "nothing"))
        for path in sorted(missing):
            report.fail("canary %s is declared for %s but not present there" % (token, path))


def main() -> int:
    report = Report()

    if not os.path.isfile(MANIFEST):
        print("manifest.json is missing", file=sys.stderr)
        return 1
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    manifest_paths: dict[str, str] = {}
    for kind in ("fixtures", "assets"):
        for entry in manifest[kind]:
            path = entry["path"]
            if path in manifest_paths:
                report.fail("duplicate manifest path: %s" % path)
            manifest_paths[path] = kind

    # 1. every manifest path exists.
    for path in sorted(manifest_paths):
        if not os.path.isfile(os.path.join(HERE, path)):
            report.fail("manifest names a missing file: %s" % path)

    # fixture ids are unique and url_path/origin are present.
    seen_ids: set[str] = set()
    for fixture in manifest["fixtures"]:
        fid = fixture["id"]
        if fid in seen_ids:
            report.fail("duplicate fixture id: %s" % fid)
        seen_ids.add(fid)
        for field in ("path", "url_path", "origin", "family",
                      "expected_semantic_fields", "sensitive_omissions",
                      "allowed_actions", "prohibited_actions",
                      "navigation_transitions", "verifier_postconditions"):
            if field not in fixture:
                report.fail("fixture %s is missing the required field %r" % (fid, field))

    # 2. every content file on disk is in the manifest.
    disk = disk_content_files()
    for path in sorted(disk):
        if path not in manifest_paths:
            report.fail("file on disk is not in the manifest: %s" % path)

    # 3. no external references.
    scan_external(report, disk)

    # 4. canaries appear only where declared.
    scan_canaries(report, manifest, disk)

    # Report.
    if report.ok():
        counts = manifest["fixture_count_by_family"]
        print("corpus %s version %s" % (manifest["corpus"], manifest["version"]))
        print("fixtures: %d  assets: %d  canaries: %d"
              % (len(manifest["fixtures"]), len(manifest["assets"]), len(manifest["canaries"])))
        print("by family: " + ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts)))
        print("content files on disk: %d (all accounted for)" % len(disk))
        print("OK: manifest complete, no external references, no canary leakage")
        return 0

    for message in report.errors:
        print("FAIL: " + message, file=sys.stderr)
    print("\n%d finding(s)" % len(report.errors), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
