# `tools/benchmark.d/`

**Status:** `[Current]` — every file here is reached by
[`../benchmark`](../benchmark). The three host-side modules run today, and the
fragment normalizer has a focused self-test; the remaining five are the
measured-run path, which has not yet been recorded as taken.
**Owning milestone:** M1, work package WP-M1-07 for the evidence record; M0,
work package WP-M0-09 for the corpus resolver.
**Authority boundary:** the parts behind [`../benchmark`](../benchmark). Three
run on any host: one *reads* the scenario corpus, one records *what was verified
and what could not run*, and one derives the release fragment from those exact
bytes. The remaining five are the device-side runner the command hands a
measured run to, and they are described as such below.

None of the three host-side files validates the corpus, and nothing here has
ever scored a run. The corpus's own self-check
([`test-fixtures/tasks/check.py`](../../test-fixtures/tasks/README.md))
is the only thing entitled to say a corpus is intact, and scoring belongs to an
execution against an installed build. The benchmark itself is defined by the
benchmark corpus and its pass values
live only in the metric registry; neither is
restated here.

The explicit `--local-loop` path is separate. [`local_loop.py`](local_loop.py)
builds and runs the release-mode host interface measurement, retaining raw
samples and host/compiler identity. It does not use the scenario scorer,
produce a release fragment, or claim a device Task Benchmark result. See
local loop performance for
the measured boundary and repeatable command.

| File | Owns | How `../benchmark` reaches it |
|---|---|---|
| [`corpus.py`](corpus.py) | Resolving the scenario corpus: what it holds, and which scenarios a suite selects | Directly, on every invocation |
| [`evidence.py`](evidence.py) | The record's shape, and the manifest fragment that cites it | Directly, on every invocation |
| [`run.py`](run.py) | Orchestrating a measured, device-side execution end to end | Directly, and only from the measured-run path |
| [`device.py`](device.py) | The `adb` device and the paths its runners and packages live at | Through `run.py` |
| [`launcher.py`](launcher.py) | Installing the test packages and driving the browser-test adapters | Through `run.py` |
| [`manifest_fragment.py`](manifest_fragment.py) | Deriving report-bound manifest scalars and raw-run attestations without claiming approval or test-report authority | Through `run.py` and `evidence.py` |
| [`scenario.py`](scenario.py) | The corpus as the runner reads it | Through `run.py` |
| [`score.py`](score.py) | Turning a completed run into a score | Through `run.py` |
| [`score_selftest.py`](score_selftest.py) | Adversarial synthetic evidence tests, without claiming a device run | Through the `fixtures-tasks` lane |

**Reachable is not the same as exercised, and this is the paragraph that keeps
the two apart.** `../benchmark` calls `corpus.py` and `evidence.py` on every
invocation and `manifest_fragment.py` when a fragment is requested. It calls
`run.py` on exactly one path, after it has satisfied
itself that one device is attached, that the device's ABI names a committed
profile, and that a Chromium checkout is recorded — and `run.py` imports
`device.py`, `launcher.py`, `scenario.py` and `score.py` in turn. The current
host now meets those three readiness conditions, but the measured path has not
run: `production` still lacks its ratified matrix, and `affected` needs an
explicit change set. The final binary seam is present:
`launcher.py` names two browser-test adapters,
`TaskBenchmarkObservationBrowserTest` and `TaskBenchmarkVerticalBrowserTest`.
The first exists — `taffy-core/test/benchmark/task_benchmark_observation_browsertest.cc`,
in `source_set("benchmark_browser_tests")` and so inside `taffy_browsertests`.
The second is
`taffy-core/test/recovery/task_benchmark_vertical_browsertest.cc`, inside the
Chrome-profile recovery source set and therefore `android_browsertests`.
Both load the selected immutable scenario bytes. The observation adapter
drives page operations; the profile adapter starts a bounded selected-page
task through the shipping Core API and reads its durable audit records. These
are incomplete scenario implementations: a completed task probe does not
establish the declared scenario outcome, all required facts, or every step.
The scorer rejects missing evidence. A record is accepted only as an exact raw
stdout line beginning with `TAFFY_BENCHMARK_RECORD_V1=`; a logging prefix or
any other decoration invalidates it. Run
`python3 tools/benchmark.d/launcher.py --self-test` to exercise that boundary.

The scorer also rejects malformed evidence, missing disclosure scans, and
audit rows with foreign task identities or non-increasing sequences. It
preserves the storage order instead of sorting away corruption. Run
`python3 tools/benchmark.d/score_selftest.py` for the synthetic positive and
adversarial cases; these tests do not produce a measured benchmark result.

A measured-run fragment derives the evidence SHA-256, run/scenario counts,
measured state, corpus version and ratified matrix id from the report it cites.
It also attests the summary as `benchmark-report` and every retained raw row as
`benchmark-run`, so release verification can re-open and compare those bytes.
It deliberately does not emit `benchmark_approved`, `benchmark_baseline`, or a
`benchmark-production` suite row: those belong to the benchmark owner and the
job carrying an attested `test-report`, while this runner produces a
`benchmark-report`. Run
`python3 tools/benchmark.d/manifest_fragment.py --self-test` to exercise that
authority boundary. Preflight fragments remain explicitly non-measured with
zero runs and scenarios.

Each adapter invocation is one attempt: the launcher overrides Chromium's
default two retries with `--test-launcher-retry-limit=0`. It deliberately keeps
the Android runner's default timeout scale. A non-default `--timeout-scale`
makes `android_browsertests` run on a worker thread, but browser startup removes
its pure-Java exception handler through a UI-thread-only path; the result is a
startup crash before the test body. The first package and test-data push can
take about 7.5 minutes on a phone. Reusing the installed test data is valid only
when the caller has separately established that those inputs are unchanged;
the benchmark launcher does not make that assumption.

## The distinction this directory exists to preserve

Three states are easy to blur into one, and a benchmark that blurs them is
worthless:

1. **The inputs are missing.** No corpus, or no change set for the suite that
   needs one. The suite says which, and selects nothing.
2. **The inputs are complete and the corpus is selected.** `corpus.py` reports
   the scenario count, the job families and the expected-outcome spread, and
   the evidence record carries `inputs_complete` and `selected_scenarios`.
3. **A run happened.** Only then is `measured` true.

This repository is in state 2. The runner and both adapters are written, but
complete scenario execution has not been demonstrated. A
production run also needs the matrix that OD-017 has not ratified. `measured`
stays `false`, and the command exits nonzero rather than turn readiness or a
rejected adapter record into a result.

## `corpus.py`

```bash
python3 tools/benchmark.d/corpus.py --corpus test-fixtures/tasks --mode summary
python3 tools/benchmark.d/corpus.py --corpus test-fixtures/tasks --mode select --suite production
```

`summary` emits one `key<TAB>value` line per fact, which is what the runner's
resolved-state block prints and what the evidence record stores. `select`
emits the scenario ids a suite would run.

The `affected` suite needs a change set (`./tools/benchmark --since <git-ref>`)
and selects three ways: a change inside a scenario's directory selects that
scenario; a change to a page fixture selects every scenario that uses it; and a
change to the corpus contract itself — the scenario manifest, its schema or its
self-check — selects everything, because that change alters what every scenario
means.

Exit status: `0` answered, `2` the corpus is absent or unreadable, `3` the
suite cannot select because the caller did not supply what it needs. Three is
not a failure of the corpus; it is a suite saying exactly what is missing.

## What this directory does not do

1. **It has never scored anything.** `score.py` is called by `run.py` and
   `run.py` is called by the command, but no invocation has reached either.
   Scoring needs the installed product on the ratified device matrix (OD-017),
   the run's audit stream read back, and real scenario execution in both
   browser-test adapters. Until all of that exists, the record says so in
   `measured_note` and the command still exits nonzero when asked to run a
   suite it cannot drive.
2. **It does not validate the corpus.** A second opinion about whether a corpus
   is well formed is a second, weaker set of rules waiting to disagree with the
   first.
3. **It does not read the metric registry.** Comparing a measurement to a
   target is a judgement with an owner; this directory is the instrument.
