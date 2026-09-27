#!/usr/bin/env python3
"""Self-check for the TaffyGo Task Benchmark scenario corpus.

Authority boundary: this file owns the filesystem and the canary scan. The
rules themselves live in `schema.py`, so the same rules that gate the committed
corpus also run against crafted data in `--self-test` without touching a disk.

It verifies six things, and fails on any of them:

  1. every scenario in the manifest exists on disk as a directory carrying a
     `scenario.json`, and every directory on disk is in the manifest — no
     orphan scenario, no phantom entry, no stray file;
  2. every page-fixture reference resolves against
     `test-fixtures/web/manifest.json`, down to the named field;
  3. every expected outcome is inside the corpus document's taxonomy, and never
     the residual label;
  4. every declared canary appears only where it should, and no token shaped
     like a canary appears anywhere it was not declared;
  5. the corpus version is coherent: it pins the page corpus version that is
     actually on disk, its own counts add up, and every scenario file agrees
     with the manifest entry beside it;
  6. every action, result code and audit event name resolves against the
     authority that owns it, so the corpus cannot invent vocabulary.

Stdlib only, read-only, no network. Exit 0 clean, 1 with findings.

    python3 test-fixtures/tasks/check.py
    python3 test-fixtures/tasks/check.py --self-test
"""

from __future__ import annotations

import copy
import json
import os
import re
import sys

# Read-only means read-only, including of this directory: importing a sibling
# module would otherwise drop a bytecode cache into the corpus this file exists
# to prove is exactly what the manifest says it is.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import schema  # noqa: E402  (the sys.path line above is what makes it importable)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(HERE, "manifest.json")
WEB_MANIFEST = os.path.join(ROOT, "test-fixtures", "web", "manifest.json")
ACTION_SCHEMA = os.path.join(
    ROOT, "taffy-core", "contracts", "bip", "schema", "action.schema.json"
)
TASK_TRANSACTION_SCHEMA = os.path.join(
    ROOT, "taffy-core", "contracts", "core-service", "schema", "task_transaction.json")

#: Files in this directory that are corpus infrastructure, not scenario data.
INFRA = {"README.md", "manifest.json", "check.py", "schema.py"}

#: The only file a scenario directory holds. One directory, one run script.
SCENARIO_FILE = "scenario.json"

#: Anything shaped like a corpus canary, declared or not. A token that looks
#: like one and is not declared is either a leak or a corpus that lost track of
#: its own secrets, and both are findings.
CANARY_SHAPE = re.compile(r"TAFFYGO-CANARY-[A-Z0-9-]+")

#: The prefix every corpus canary carries. The self-test builds its counterexample
#: tokens from this rather than spelling one out, because the scan above is
#: absolute: it applies to this file too, and a rule that exempts its own tests is
#: a rule with a hole in it.
CANARY_PREFIX = "TAFFYGO-CANARY-"

#: The enumeration in that schema whose members are the task events a scenario
#: may expect. The authority used to be a heading in an architecture document,
#: which read well and was the wrong file twice over: a prose list drifts from
#: the enumeration it describes, and the planning documents are this
#: repository's own, so a tree published without them failed this whole lane on
#: a missing file. The schema is the generator's own input, the contracts lane
#: already checks it, and it ships wherever the corpus does.
EVENTS_ENUM = "PersistedAuditEventType"


def read_json(path, findings, label):
    if not os.path.isfile(path):
        findings.append("%s is missing: %s" % (label, os.path.relpath(path, ROOT)))
        return None
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except ValueError as error:
        findings.append("%s is not valid JSON: %s" % (label, error))
        return None


def read_audit_events(findings):
    """The task event names, read from the contract that owns them."""
    schema = read_json(TASK_TRANSACTION_SCHEMA, findings,
                       "the core-service task-transaction schema")
    if schema is None:
        return set()
    enums = schema.get("types", {}).get("enums", [])
    for entry in enums:
        if entry.get("name") != EVENTS_ENUM:
            continue
        names = {member["name"] for member in entry.get("members", []) if "name" in member}
        if not names:
            findings.append("%s carries no members, so no audit event name can be resolved"
                            % EVENTS_ENUM)
        return names
    findings.append("the core-service task-transaction schema no longer carries %r, so the "
                    "corpus cannot resolve an event name against it" % EVENTS_ENUM)
    return set()


def load_authorities(findings):
    """The four vocabularies the schema validates against."""
    web = read_json(WEB_MANIFEST, findings, "the page corpus manifest")
    action = read_json(ACTION_SCHEMA, findings, "the wire contract's action schema")
    if web is None or action is None:
        return None
    defs = action.get("$defs", {})
    for name in ("ActionType", "ActionResultCode"):
        if not defs.get(name, {}).get("enum"):
            findings.append("the wire contract's action schema defines no %s enumeration; the "
                            "corpus refuses to fall back to a copy of it" % name)
            return None
    return schema.Authorities(
        fixture_fields={f["id"]: {e["field"] for e in f["expected_semantic_fields"]}
                        for f in web["fixtures"]},
        fixture_origins={f["id"]: f["origin"] for f in web["fixtures"]},
        canaries={c["token"]: set(c["carried_by"]) for c in web["canaries"]},
        origins=set(web["origins"]),
        action_types=set(defs["ActionType"]["enum"]),
        result_codes=set(defs["ActionResultCode"]["enum"]),
        audit_events=read_audit_events(findings),
        page_corpus_version=web["version"],
    )


def load_scenarios(manifest, findings):
    """Manifest entries and on-disk directories, checked against each other."""
    payloads = {}
    expected_dirs = {}
    for entry in manifest.get("scenarios", []):
        expected_dirs[entry.get("directory")] = entry.get("id")

    on_disk = set()
    for name in sorted(os.listdir(HERE)):
        path = os.path.join(HERE, name)
        if name.startswith(".") or name == "__pycache__":
            continue
        if os.path.isdir(path):
            on_disk.add(name)
        elif name not in INFRA:
            findings.append("stray file in the corpus root: %s" % name)

    for name in sorted(on_disk - set(expected_dirs)):
        findings.append("scenario directory %s is not listed in the manifest; an unlisted "
                        "scenario is one no report can cite" % name)

    for directory, sid in sorted(expected_dirs.items()):
        path = os.path.join(HERE, directory)
        if directory not in on_disk:
            findings.append("%s: the manifest names directory %s, which does not exist"
                            % (sid, directory))
            continue
        for name in sorted(os.listdir(path)):
            if name.startswith(".") or name == "__pycache__":
                continue
            if name != SCENARIO_FILE:
                findings.append("%s: unexpected file %s/%s; a scenario directory holds only %s"
                                % (sid, directory, name, SCENARIO_FILE))
        payload = read_json(os.path.join(path, SCENARIO_FILE), findings,
                            "%s's run script" % sid)
        if payload is not None:
            payloads[sid] = payload
    return payloads


def scan_canaries(manifest, authorities, findings):
    """A canary lives in the manifest's own declaration and nowhere else here.

    The page corpus proves each token stays inside the fixture that carries it.
    This corpus has the mirror obligation: a scenario names the tokens it seeds
    so a run can be scored for leakage, and naming them anywhere else — in a
    scenario's prose, in the README, in a second scenario's directory — would
    put the secret into the instrument that is supposed to be watching for it.
    """
    declared = {}
    for entry in manifest.get("scenarios", []):
        for token in entry.get("seeded_canaries", []):
            declared[token] = declared.get(token, 0) + 1

    for dirpath, dirnames, filenames in os.walk(HERE):
        dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "__pycache__"]
        for name in sorted(filenames):
            if name.startswith("."):
                continue
            path = os.path.join(dirpath, name)
            relative = os.path.relpath(path, HERE).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            found = CANARY_SHAPE.findall(text)
            if not found:
                continue
            if relative != "manifest.json":
                findings.append("%s carries canary token(s) %s; only the manifest's own "
                                "seeded_canaries declaration may name one"
                                % (relative, ", ".join(sorted(set(found)))))
                continue
            for token in sorted(set(found)):
                if token not in authorities.canaries:
                    findings.append("manifest.json names %s, which the page corpus does not "
                                    "declare as a canary" % token)
                elif found.count(token) != declared.get(token, 0):
                    findings.append("manifest.json mentions %s %d time(s) but declares it as "
                                    "seeded %d time(s); a canary in prose is a canary in the "
                                    "instrument" % (token, found.count(token), declared.get(token, 0)))


def report(manifest, authorities, findings):
    counts = manifest["scenario_count_by_job_family"]
    outcomes = manifest["scenario_count_by_expected_outcome"]
    print("corpus %s version %s" % (manifest["corpus"], manifest["version"]))
    print("page corpus %s version %s (pinned)"
          % (manifest["page_corpus"]["corpus"], manifest["page_corpus"]["required_version"]))
    print("scenarios: %d  job families: %d  page fixtures referenced: %d of %d"
          % (manifest["scenario_count"], len(manifest["job_families"]),
             manifest["page_fixtures_used"], len(authorities.fixture_fields)))
    print("by job family: " + ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts)))
    print("by expected outcome: " + ", ".join("%s=%d" % (k, outcomes[k]) for k in sorted(outcomes)))
    unused = sorted(set(authorities.fixture_fields) - {
        fid for entry in manifest["scenarios"] for fid in entry["page_fixtures"]})
    if unused:
        print("page fixtures no scenario exercises: " + ", ".join(unused))
    print("scripted model outputs: %s" % manifest["scripted_model_outputs"]["state"])
    print("execution: %s" % manifest["execution"]["state"])
    print("OK: manifest complete, every reference resolves, no canary outside its declaration")


def load(findings):
    """The whole corpus: authorities, manifest, run scripts. None on a hard stop."""
    authorities = load_authorities(findings)
    manifest = read_json(MANIFEST, findings, "the scenario corpus manifest")
    if authorities is None or manifest is None:
        return None, None, None
    payloads = load_scenarios(manifest, findings)
    return authorities, manifest, payloads


def drop_missing_scope(manifest, payloads):
    """Take the stated gap off the first scenario that owes one."""
    for entry in manifest["scenarios"]:
        if entry["expected_outcome"] == "verified-partial":
            entry.pop("missing_scope")
            return


def self_test():
    """Prove each rule fires, by breaking the real corpus one way at a time."""
    findings = []
    authorities, manifest, payloads = load(findings)
    if manifest is None or findings:
        print("self-test cannot run: the corpus itself does not load", file=sys.stderr)
        for message in findings:
            print("FAIL: " + message, file=sys.stderr)
        return 1

    def broken(label, mutate):
        bad_manifest = copy.deepcopy(manifest)
        bad_payloads = copy.deepcopy(payloads)
        mutate(bad_manifest, bad_payloads)
        out = []
        schema.check_corpus(bad_manifest, bad_payloads, authorities, out)
        return label, out

    def first(bad):
        return bad["scenarios"][0]

    # A real canary whose carrying fixture the first scenario does not load. Seeding
    # it there is the "declared but unreachable" case, and picking it from the page
    # corpus keeps the token out of this file's own text.
    reachable = set(first(manifest)["page_fixtures"])
    uncarried = next(token for token, carriers in sorted(authorities.canaries.items())
                     if not carriers & reachable)

    cases = [
        broken("an outcome label outside the taxonomy",
               lambda m, p: first(m).update(expected_outcome="mostly-fine")),
        broken("the residual label declared as expected",
               lambda m, p: first(m).update(expected_outcome=m["outcome_labels"]["residual"])),
        broken("a page fixture that does not exist",
               lambda m, p: first(m)["page_fixtures"].append("no-such-fixture")),
        broken("a page field the fixture does not declare",
               lambda m, p: first(m)["required_facts"][0].update(source_field="no.such.field")),
        broken("a canary the page corpus never declared",
               lambda m, p: first(m)["seeded_canaries"].append(CANARY_PREFIX + "INVENTED-000000")),
        broken("a seeded canary no fixture in the run carries",
               lambda m, p: first(m)["seeded_canaries"].append(uncarried)),
        broken("a refusal expecting a result code the contract has no name for",
               lambda m, p: first(m)["refused_actions"][0].update(expected_result_code="MAYBE")),
        broken("an action name with no namespace",
               lambda m, p: first(m)["allowed_actions"].append("ACTIVATE")),
        broken("a step the scenario never permitted",
               lambda m, p: p[first(m)["id"]]["steps"][0].update(kind="action:SUBMIT_FORM")),
        broken("a required fact no step produces",
               lambda m, p: first(m)["required_facts"].append(dict(
                   first(m)["required_facts"][0], id="F-unreachable"))),
        broken("a scenario count that drifted from the array",
               lambda m, p: m.update(scenario_count=m["scenario_count"] + 1)),
        broken("a page corpus version the corpus no longer pins",
               lambda m, p: m["page_corpus"].update(required_version="0.0.1")),
        broken("an audit event the task-transaction contract does not list",
               lambda m, p: p[first(m)["id"]]["expected_audit_events"].append("TaskVibed")),
        broken("a verified-partial scenario with no missing scope", drop_missing_scope),
        broken("a run script whose version disagrees with the manifest",
               lambda m, p: p[first(m)["id"]].update(version="9.9.9")),
        broken("a manifest entry with no run script",
               lambda m, p: p.pop(first(m)["id"])),
    ]

    failures = 0
    for label, out in cases:
        if out:
            print("  detected: %s" % label)
        else:
            print("  NOT DETECTED: %s" % label, file=sys.stderr)
            failures += 1

    clean = []
    schema.check_corpus(manifest, payloads, authorities, clean)
    if clean:
        print("  NOT DETECTED: the unmodified corpus must produce no finding", file=sys.stderr)
        for message in clean:
            print("    " + message, file=sys.stderr)
        failures += 1
    else:
        print("  detected: the unmodified corpus produces no finding")

    if failures:
        print("\n%d self-test case(s) did not fire" % failures, file=sys.stderr)
        return 1
    print("\nself-test: %d rule(s) fire on a broken corpus and none on the real one"
          % len(cases))
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()

    findings = []
    authorities, manifest, payloads = load(findings)
    if manifest is not None:
        schema.check_corpus(manifest, payloads, authorities, findings)
        scan_canaries(manifest, authorities, findings)

    if not findings:
        report(manifest, authorities, findings)
        return 0

    for message in findings:
        print("FAIL: " + message, file=sys.stderr)
    print("\n%d finding(s)" % len(findings), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
