#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Resolve the Task Benchmark scenario corpus for `./tools/benchmark`.

Authority boundary: this module *reads* the scenario corpus and answers two
questions — what does it hold, and which of its scenarios does a suite select.
It validates nothing (`test-fixtures/tasks/check.py` is the corpus's own
self-check and the only thing entitled to say the corpus is intact), it scores
nothing, and it restates no pass value. Keeping resolution here and validation
there means the runner cannot drift into a second, weaker opinion about whether
a corpus is well formed.

Owning milestone: M0, work package WP-M0-09, alongside the command suite.

Two modes, both stdlib-only and read-only:

  summary   one `key<TAB>value` line per fact about the corpus, for the
            runner's resolved-state block and its evidence record;
  select    the scenario ids a suite would run, one per line.

Exit status: 0 answered, 2 the corpus is absent or unreadable, 3 the suite
cannot select because the caller did not supply what it needs. Three is not a
failure of the corpus; it is a suite saying exactly what is missing, which is
what the runner prints.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

EXIT_OK = 0
EXIT_NO_CORPUS = 2
EXIT_CANNOT_SELECT = 3

MANIFEST = "manifest.json"


def load(corpus_dir):
    """The corpus manifest, or a message saying why it could not be read."""
    path = os.path.join(corpus_dir, MANIFEST)
    if not os.path.isdir(corpus_dir):
        return None, "the scenario corpus directory %s does not exist" % corpus_dir
    if not os.path.isfile(path):
        return None, "no scenario manifest at %s" % path
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle), None
    except ValueError as error:
        return None, "the scenario manifest is not valid JSON: %s" % error


def fixture_paths(web_manifest):
    """fixture id -> repository-relative path, for the affected-suite selection."""
    if not os.path.isfile(web_manifest):
        return {}
    try:
        with open(web_manifest, encoding="utf-8") as handle:
            web = json.load(handle)
    except ValueError:
        return {}
    base = os.path.dirname(web_manifest)
    return {f["id"]: os.path.join(base, f["path"]) for f in web["fixtures"]}


def summary(manifest, corpus_dir, out) -> None:
    """One fact per line. The runner reads these; nothing here is computed twice."""
    families = manifest.get("scenario_count_by_job_family", {})
    outcomes = manifest.get("scenario_count_by_expected_outcome", {})
    page = manifest.get("page_corpus", {})
    rows = [
        ("corpus", manifest.get("corpus", "")),
        ("version", manifest.get("version", "")),
        ("directory", corpus_dir),
        ("scenarios", str(manifest.get("scenario_count", 0))),
        ("job_families", str(len(manifest.get("job_families", [])))),
        ("page_corpus", page.get("corpus", "")),
        ("page_corpus_version", page.get("required_version", "")),
        ("page_fixtures_used", str(manifest.get("page_fixtures_used", 0))),
        ("scripted_model_outputs", manifest.get("scripted_model_outputs", {}).get("state", "")),
        ("execution_state", manifest.get("execution", {}).get("state", "")),
        ("by_job_family", ", ".join("%s=%d" % (k, families[k]) for k in sorted(families))),
        ("by_expected_outcome", ", ".join("%s=%d" % (k, outcomes[k]) for k in sorted(outcomes))),
    ]
    for key, value in rows:
        out.write("%s\t%s\n" % (key, value))
    for need in manifest.get("execution", {}).get("needs", []):
        out.write("execution_needs\t%s\n" % need)


def changed_paths(argument):
    """The caller's change set: a file of paths, or `-` for standard input."""
    if argument == "-":
        return [line.strip() for line in sys.stdin.read().splitlines() if line.strip()]
    with open(argument, encoding="utf-8") as handle:
        return [line.strip() for line in handle.read().splitlines() if line.strip()]


def select_affected(manifest, corpus_dir, repo_root, web_manifest, changed):
    """The scenarios a change set touches, or the whole corpus when it touches the corpus.

    Three ways a change reaches a scenario, and the first two are deliberately
    blunt: a change to the corpus contract or to the page corpus index changes
    what every scenario means, so selecting a subset there would measure less
    than the change affects.
    """
    def relative(path):
        return os.path.relpath(path, repo_root).replace(os.sep, "/")

    corpus_rel = relative(corpus_dir)
    web_rel = relative(web_manifest)
    wholesale = (corpus_rel + "/" + MANIFEST, corpus_rel + "/schema.py",
                 corpus_rel + "/check.py", web_rel)
    if any(path in wholesale for path in changed):
        return [entry["id"] for entry in manifest["scenarios"]]

    paths = fixture_paths(web_manifest)
    selected = []
    for entry in manifest["scenarios"]:
        prefix = corpus_rel + "/" + entry["directory"] + "/"
        touched = any(path.startswith(prefix) for path in changed)
        if not touched:
            fixture_files = {relative(paths[fid]) for fid in entry["page_fixtures"]
                             if fid in paths}
            touched = bool(fixture_files & set(changed))
        if touched:
            selected.append(entry["id"])
    return selected


def main(argv):
    parser = argparse.ArgumentParser(add_help=True, description=__doc__)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--repo-root", default=os.getcwd())
    parser.add_argument("--web-manifest", default="")
    parser.add_argument("--mode", choices=("summary", "select"), required=True)
    parser.add_argument("--suite", default="")
    parser.add_argument("--changed", default="",
                        help="file of changed repository paths, or - for stdin")
    args = parser.parse_args(argv)

    manifest, problem = load(args.corpus)
    if manifest is None:
        sys.stderr.write(problem + "\n")
        return EXIT_NO_CORPUS

    if args.mode == "summary":
        summary(manifest, args.corpus, sys.stdout)
        return EXIT_OK

    if args.suite == "production":
        for entry in manifest["scenarios"]:
            sys.stdout.write(entry["id"] + "\n")
        return EXIT_OK

    if args.suite == "affected":
        if not args.changed:
            sys.stderr.write(
                "no change set: the affected suite runs only the scenarios a change "
                "touches, so it needs one. Pass --since <git-ref> to ./tools/benchmark.\n")
            return EXIT_CANNOT_SELECT
        web = args.web_manifest or os.path.join(
            args.repo_root, "test-fixtures", "web", MANIFEST)
        for scenario in select_affected(manifest, args.corpus, args.repo_root, web,
                                        changed_paths(args.changed)):
            sys.stdout.write(scenario + "\n")
        return EXIT_OK

    sys.stderr.write("unknown suite: %s\n" % args.suite)
    return EXIT_CANNOT_SELECT


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
