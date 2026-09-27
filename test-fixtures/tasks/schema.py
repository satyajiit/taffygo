#!/usr/bin/env python3
"""The Task Benchmark scenario schema, as executable rules.

Authority boundary: this module decides *whether a loaded corpus is well
formed*. It reads nothing from disk, resolves no path, and scores no run —
`check.py` owns the filesystem and the canary scan, and scoring belongs to an
execution against an installed build. Keeping the rules here and the I/O there
is what lets the self-check exercise every rule against crafted data without
writing a file.

Owning milestone: M0, work package WP-M0-08 (the page corpus's twin).

The four authorities this module validates against are supplied by the caller,
never looked up here, because each of them owns its own vocabulary:

  - the page corpus manifest owns fixture ids, their fields, their origins and
    their canaries;
  - `taffy-core/contracts/bip/schema/action.schema.json` owns action types and action
    result codes;
  - `docs/architecture/domain-model.md` owns plan-step kinds, control modes,
    budget keys and task event names;
  - `docs/quality/benchmark-corpus.md` owns the outcome taxonomy, which the
    manifest carries as data so that a scenario declaring a label outside it
    fails here rather than at adjudication time.

Stdlib only. Read-only. Every function appends strings to a findings list and
returns nothing; a caller with an empty list has a corpus that holds together.
"""

from __future__ import annotations

import re

#: Top-level keys the manifest must carry. A missing one is a corpus that
#: cannot be validated at all, so it is reported before anything else runs.
MANIFEST_KEYS = (
    "corpus", "title", "version", "immutable", "authority", "versioning_rule",
    "self_check", "page_corpus", "outcome_labels", "claim_types",
    "evidence_kinds", "tolerances", "control_modes", "budget_keys",
    "action_vocabulary", "result_codes", "audit_events",
    "scripted_model_outputs", "execution", "job_families", "scenarios",
    "scenario_count", "scenario_count_by_job_family",
    "scenario_count_by_expected_outcome", "page_fixtures_used",
)

#: Keys every scenario entry carries. `missing_scope` is conditional and is
#: checked separately, against the expected outcome.
SCENARIO_KEYS = (
    "id", "directory", "job_family", "title", "user_goal", "use_cases",
    "page_fixtures", "starting_fixture", "control_mode", "source_scope",
    "budgets", "required_facts", "forbidden_facts", "seeded_conflicts",
    "seeded_canaries", "allowed_actions", "refused_actions",
    "expected_outcome", "partial_allowed", "expected_artifact",
    "adjudication_notes",
)

VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+$")
SCENARIO_ID = re.compile(r"^TB-\d{3}$")
NAMESPACES = ("step", "action", "tool")

#: The corpus document says a scenario declares its expected terminal label in
#: advance. A scenario that declared the residual label would be declaring in
#: advance that the product must fail, which measures nothing; the residual
#: label exists for what a run collapses to, not for what a corpus expects.
RESIDUAL_IS_NEVER_EXPECTED = (
    "the residual label records what a run collapses to; a scenario that "
    "expects it has stopped measuring anything"
)


class Authorities:
    """The vocabularies this module validates against, supplied by the caller."""

    def __init__(self, fixture_fields, fixture_origins, canaries, origins,
                 action_types, result_codes, audit_events, page_corpus_version):
        #: fixture id -> set of expected semantic field names
        self.fixture_fields = fixture_fields
        #: fixture id -> origin name
        self.fixture_origins = fixture_origins
        #: canary token -> set of fixture ids declared to carry it
        self.canaries = canaries
        self.origins = origins
        self.action_types = action_types
        self.result_codes = result_codes
        self.audit_events = audit_events
        self.page_corpus_version = page_corpus_version


def _missing(found, expected, label, out):
    for key in expected:
        if key not in found:
            out.append("%s is missing the required key %r" % (label, key))


def check_manifest(manifest, authorities, out) -> None:
    """The corpus header: its version, its vocabularies, and its own counts."""
    _missing(manifest, MANIFEST_KEYS, "manifest.json", out)
    version = manifest.get("version", "")
    if not VERSION_PATTERN.match(str(version)):
        out.append("manifest version %r is not a three-part version" % version)
    if manifest.get("immutable") is not True:
        out.append("the corpus must declare itself immutable")

    page = manifest.get("page_corpus", {})
    if page.get("required_version") != authorities.page_corpus_version:
        out.append(
            "the corpus pins page corpus version %r but the page corpus is at %r; "
            "a page-corpus bump is a scenario-corpus bump"
            % (page.get("required_version"), authorities.page_corpus_version))

    labels = manifest.get("outcome_labels", {})
    if not labels.get("labels"):
        out.append("the outcome taxonomy is empty")
    if labels.get("residual") not in labels.get("labels", []):
        out.append("the residual label is not part of the outcome taxonomy")

    scripted = manifest.get("scripted_model_outputs", {})
    if scripted.get("state") not in ("not-recorded", "recorded"):
        out.append("scripted model outputs must be recorded or not-recorded, not %r"
                   % scripted.get("state"))

    families = [f.get("id") for f in manifest.get("job_families", [])]
    if len(set(families)) != len(families):
        out.append("two job families share an id")
    items = [f.get("corpus_item") for f in manifest.get("job_families", [])]
    if sorted(items) != list(range(1, len(items) + 1)):
        out.append("job families must map onto the corpus document's numbered list "
                   "one for one; got items %r" % (items,))


def check_counts(manifest, out) -> None:
    """The manifest's own arithmetic. A count that drifts hides a lost scenario."""
    scenarios = manifest.get("scenarios", [])
    if manifest.get("scenario_count") != len(scenarios):
        out.append("scenario_count says %r, the array holds %d"
                   % (manifest.get("scenario_count"), len(scenarios)))

    by_family = {}
    by_outcome = {}
    used = set()
    for entry in scenarios:
        by_family[entry.get("job_family")] = by_family.get(entry.get("job_family"), 0) + 1
        by_outcome[entry.get("expected_outcome")] = by_outcome.get(entry.get("expected_outcome"), 0) + 1
        used.update(entry.get("page_fixtures", []))
    if manifest.get("scenario_count_by_job_family") != by_family:
        out.append("scenario_count_by_job_family disagrees with the scenarios: %r vs %r"
                   % (manifest.get("scenario_count_by_job_family"), by_family))
    if manifest.get("scenario_count_by_expected_outcome") != by_outcome:
        out.append("scenario_count_by_expected_outcome disagrees with the scenarios: %r vs %r"
                   % (manifest.get("scenario_count_by_expected_outcome"), by_outcome))
    if manifest.get("page_fixtures_used") != len(used):
        out.append("page_fixtures_used says %r, the scenarios reference %d"
                   % (manifest.get("page_fixtures_used"), len(used)))

    for family in manifest.get("job_families", []):
        if by_family.get(family.get("id"), 0) == 0:
            out.append("job family %s has no scenario; the corpus document names it, so "
                       "the corpus must exercise it" % family.get("id"))


def resolve_action(name, manifest, authorities, where, out) -> None:
    """One `<namespace>:<name>` token, resolved against the namespace's owner."""
    prefix, sep, rest = str(name).partition(":")
    if not sep or prefix not in NAMESPACES:
        out.append("%s: %r has no known namespace prefix (%s)"
                   % (where, name, "/".join(NAMESPACES)))
        return
    vocabulary = manifest.get("action_vocabulary", {})
    if prefix == "step" and rest not in vocabulary.get("step", {}).get("names", []):
        out.append("%s: %r is not a plan-step kind the domain model defines" % (where, name))
    elif prefix == "action" and rest not in authorities.action_types:
        out.append("%s: %r is not an action type the wire contract defines" % (where, name))
    elif prefix == "tool" and rest not in vocabulary.get("tool", {}).get("names", []):
        out.append("%s: %r is not a tool the corpus declares" % (where, name))


def check_facts(entry, manifest, authorities, out) -> list:
    """Required and forbidden facts. Returns the required-fact ids, for coverage."""
    sid = entry.get("id")
    fixtures = entry.get("page_fixtures", [])
    fact_ids = []
    for fact in entry.get("required_facts", []):
        fid = fact.get("id")
        if fid in fact_ids:
            out.append("%s: duplicate required-fact id %r" % (sid, fid))
        fact_ids.append(fid)
        if fact.get("claim_type") not in manifest.get("claim_types", {}).get("values", []):
            out.append("%s/%s: claim type %r is outside the declared set"
                       % (sid, fid, fact.get("claim_type")))
        if fact.get("tolerance") not in manifest.get("tolerances", {}).get("values", []):
            out.append("%s/%s: tolerance %r is outside the declared set"
                       % (sid, fid, fact.get("tolerance")))
        evidence = fact.get("evidence")
        if evidence not in manifest.get("evidence_kinds", {}).get("values", []):
            out.append("%s/%s: evidence kind %r is outside the declared set"
                       % (sid, fid, evidence))
        elif evidence == "page-field":
            source = fact.get("source_fixture")
            if source not in fixtures:
                out.append("%s/%s: source fixture %r is not one of this scenario's fixtures"
                           % (sid, fid, source))
            elif fact.get("source_field") not in authorities.fixture_fields.get(source, set()):
                out.append("%s/%s: the page corpus declares no field %r on fixture %r"
                           % (sid, fid, fact.get("source_field"), source))
        elif fact.get("source_fixture") is not None or fact.get("source_field") is not None:
            out.append("%s/%s: a fact checked against %s must not name a page source"
                       % (sid, fid, evidence))

    forbidden_ids = []
    for fact in entry.get("forbidden_facts", []):
        if fact.get("id") in forbidden_ids:
            out.append("%s: duplicate forbidden-fact id %r" % (sid, fact.get("id")))
        forbidden_ids.append(fact.get("id"))
        if not fact.get("reason"):
            out.append("%s/%s: a forbidden fact carries the reason it is forbidden"
                       % (sid, fact.get("id")))
    if not forbidden_ids:
        out.append("%s: declares nothing it must not contain, so a leak or an invention "
                   "would pass it" % sid)

    for conflict in entry.get("seeded_conflicts", []):
        for ref in conflict.get("between", []):
            if ref not in fact_ids:
                out.append("%s/%s: the seeded conflict names unknown fact %r"
                           % (sid, conflict.get("id"), ref))
    return fact_ids


def check_scenario_entry(entry, manifest, authorities, out) -> None:
    """One manifest scenario entry, end to end."""
    sid = entry.get("id", "<no id>")
    _missing(entry, SCENARIO_KEYS, "scenario %s" % sid, out)
    if not SCENARIO_ID.match(str(sid)):
        out.append("scenario id %r does not match the corpus's TB-nnn form" % sid)
    if not str(entry.get("directory", "")).startswith(str(sid) + "-"):
        out.append("%s: directory %r must begin with the scenario id"
                   % (sid, entry.get("directory")))
    if entry.get("job_family") not in [f.get("id") for f in manifest.get("job_families", [])]:
        out.append("%s: job family %r is not declared" % (sid, entry.get("job_family")))
    if entry.get("control_mode") not in manifest.get("control_modes", {}).get("values", []):
        out.append("%s: control mode %r is outside the declared set"
                   % (sid, entry.get("control_mode")))

    fixtures = entry.get("page_fixtures", [])
    if len(set(fixtures)) != len(fixtures):
        out.append("%s: the same page fixture is listed twice" % sid)
    for fid in fixtures:
        if fid not in authorities.fixture_fields:
            out.append("%s: page fixture %r does not exist in the page corpus" % (sid, fid))
    if not fixtures:
        out.append("%s: a scenario runs against at least one page fixture" % sid)
    elif entry.get("starting_fixture") not in fixtures:
        out.append("%s: the starting fixture %r is not among this scenario's fixtures"
                   % (sid, entry.get("starting_fixture")))

    for origin in entry.get("source_scope", {}).get("origins", []):
        if origin not in authorities.origins:
            out.append("%s: source scope names unknown origin %r" % (sid, origin))
    for key in entry.get("budgets", {}):
        if key not in manifest.get("budget_keys", {}).get("values", []):
            out.append("%s: budget key %r is not one the domain model defines" % (sid, key))

    check_facts(entry, manifest, authorities, out)
    check_canaries(entry, authorities, out)
    check_actions(entry, manifest, authorities, out)
    check_outcome(entry, manifest, out)


def check_canaries(entry, authorities, out) -> None:
    """A seeded canary must exist, and a fixture carrying it must be in the run."""
    sid = entry.get("id")
    for token in entry.get("seeded_canaries", []):
        carriers = authorities.canaries.get(token)
        if carriers is None:
            out.append("%s: %r is not a canary the page corpus declares" % (sid, token))
        elif not carriers & set(entry.get("page_fixtures", [])):
            out.append("%s: canary %s is seeded but no fixture that carries it is used, "
                       "so the scenario cannot leak it and proves nothing" % (sid, token))


def check_actions(entry, manifest, authorities, out) -> None:
    """The permission set, the refusal set, and the codes refusals expect."""
    sid = entry.get("id")
    for name in entry.get("allowed_actions", []):
        resolve_action(name, manifest, authorities, "%s allowed_actions" % sid, out)
    seen = set()
    for refusal in entry.get("refused_actions", []):
        name = refusal.get("name")
        resolve_action(name, manifest, authorities, "%s refused_actions" % sid, out)
        code = refusal.get("expected_result_code")
        if code not in authorities.result_codes:
            out.append("%s: %r expects result code %r, which the wire contract does not define"
                       % (sid, name, code))
        if (name, code) in seen:
            out.append("%s: the refusal %r/%r is declared twice" % (sid, name, code))
        seen.add((name, code))
        if not refusal.get("reason"):
            out.append("%s: the refusal of %r carries no reason" % (sid, name))
    if not entry.get("refused_actions"):
        out.append("%s: declares no refused action, so it measures no boundary" % sid)


def check_outcome(entry, manifest, out) -> None:
    """The expected terminal label, and the two fields that must agree with it."""
    sid = entry.get("id")
    labels = manifest.get("outcome_labels", {})
    outcome = entry.get("expected_outcome")
    if outcome not in labels.get("labels", []):
        out.append("%s: expected outcome %r is outside the corpus document's taxonomy (%s)"
                   % (sid, outcome, ", ".join(labels.get("labels", []))))
        return
    if outcome == labels.get("residual"):
        out.append("%s: expected outcome %r is the residual label — %s"
                   % (sid, outcome, RESIDUAL_IS_NEVER_EXPECTED))
        return
    partial = outcome == "verified-partial"
    if bool(entry.get("partial_allowed")) != partial:
        out.append("%s: partial_allowed is %r for expected outcome %r"
                   % (sid, entry.get("partial_allowed"), outcome))
    if ("missing_scope" in entry) != partial:
        out.append("%s: a verified-partial scenario states its missing scope and no other "
                   "scenario declares one; outcome %r, missing_scope %s"
                   % (sid, outcome, "present" if "missing_scope" in entry else "absent"))
    if len(entry.get("adjudication_notes", [])) < 2:
        out.append("%s: two reviewers settle a disputed label, and they need more than one "
                   "note to do it" % sid)


def check_scenario_file(entry, payload, manifest, authorities, out) -> None:
    """The run script beside the contract: steps, events, and the labeler's rubric."""
    sid = entry.get("id")
    for key, expected in (("scenario", sid),
                          ("corpus", manifest.get("corpus")),
                          ("version", manifest.get("version")),
                          ("job_family", entry.get("job_family"))):
        if payload.get(key) != expected:
            out.append("%s/scenario.json: %s says %r, the manifest says %r"
                       % (sid, key, payload.get(key), expected))
    if "scripted_model_outputs" in payload and \
            manifest.get("scripted_model_outputs", {}).get("state") == "not-recorded":
        out.append("%s/scenario.json carries scripted model outputs while the corpus records "
                   "none; recording them is a version bump" % sid)

    fact_ids = [f.get("id") for f in entry.get("required_facts", [])]
    fixtures = entry.get("page_fixtures", [])
    allowed = entry.get("allowed_actions", [])
    produced = set()
    steps = payload.get("steps", [])
    if not steps:
        out.append("%s/scenario.json has no steps" % sid)
    for index, step in enumerate(steps, start=1):
        where = "%s/scenario.json step %d" % (sid, index)
        if step.get("n") != index:
            out.append("%s: numbered %r out of order" % (where, step.get("n")))
        resolve_action(step.get("kind"), manifest, authorities, where, out)
        if step.get("kind") not in allowed:
            out.append("%s: %r is not in this scenario's allowed_actions"
                       % (where, step.get("kind")))
        if step.get("fixture") is not None and step.get("fixture") not in fixtures:
            out.append("%s: fixture %r is not one of this scenario's fixtures"
                       % (where, step.get("fixture")))
        for ref in step.get("produces", []):
            if ref not in fact_ids:
                out.append("%s: produces unknown fact %r" % (where, ref))
            produced.add(ref)

    for orphan in [f for f in fact_ids if f not in produced]:
        out.append("%s/scenario.json: no step produces required fact %r, so a run could pass "
                   "the script without ever reaching it" % (sid, orphan))

    for event in payload.get("injected_events", []):
        target = event.get("before_step")
        if not isinstance(target, int) or not 1 <= target <= len(steps):
            out.append("%s/scenario.json: injected event %r targets step %r, which does not "
                       "exist" % (sid, event.get("event"), target))

    events = payload.get("expected_audit_events", [])
    if not events:
        out.append("%s/scenario.json expects no audit event; the audit stream is where most "
                   "non-page facts are checked" % sid)
    for event in events:
        if event not in authorities.audit_events:
            out.append("%s/scenario.json: %r is not a task event the domain model lists"
                       % (sid, event))

    rubric = payload.get("adjudication_rubric", [])
    if not rubric:
        out.append("%s/scenario.json carries no rubric, so a disputed run has nothing to be "
                   "settled against" % sid)
    for item in rubric:
        if not item.get("dispute") or not item.get("resolution"):
            out.append("%s/scenario.json: a rubric entry needs both a dispute and its "
                       "resolution" % sid)


def check_corpus(manifest, payloads, authorities, out) -> None:
    """Every rule in this module, over a whole loaded corpus."""
    check_manifest(manifest, authorities, out)
    check_counts(manifest, out)
    seen_ids, seen_dirs = set(), set()
    for entry in manifest.get("scenarios", []):
        sid = entry.get("id")
        if sid in seen_ids:
            out.append("two scenarios share the id %r" % sid)
        seen_ids.add(sid)
        if entry.get("directory") in seen_dirs:
            out.append("two scenarios share the directory %r" % entry.get("directory"))
        seen_dirs.add(entry.get("directory"))
        check_scenario_entry(entry, manifest, authorities, out)
        payload = payloads.get(sid)
        if payload is None:
            out.append("%s: the manifest lists it but no scenario.json was loaded" % sid)
        else:
            check_scenario_file(entry, payload, manifest, authorities, out)
