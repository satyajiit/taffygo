# Python tool worker

**Status:** `[Proposed]` the sandboxed Python worker of decisions
0007 and
0040, over
the vendored interpreter of decisions
0046,
0066
and
0067.
**Implementation status:** the service, its admission and the executor are
written and they **compile** — for Android arm64 and x64, and the complete
`taffy_browsertests` APK linked around them. That is the whole of it.
`PythonToolWorkerBrowserTest`
in `taffy-core/test/tool/python_tool_worker_browsertest.cc` has never been
executed, so nothing on this page about *behaviour* is evidence; it is a
description of code that compiles. The new
`PythonToolWorkerSp07BrowserTest` cases are written but have not yet been
compiled or run; they challenge ambient file/environment access, unavailable
network/native/subprocess imports, the counting allocator, CPU deadline and
hard-cancellation process exit, then require a fresh worker to recover. The
verification report
records exactly that, and the on-device sandbox, cancellation-timing and
memory-pressure results SP-07 asks for are absent.
**Owning work package:**
WP-M7-02
— the Python tool worker. **Owning milestone:** M7.

## Authority boundary

This directory owns **one utility process's willingness to run one registered
entrypoint over bounded bytes**, and the interpreter lifetime around it. It
owns nothing else.

- It is **not the authority on which entrypoints exist**. That is
  [`taffy-core/components/tools/entrypoints`](../../../components/tools/entrypoints/README.md),
  which holds the source, the generator and the readable table.
- It **grants no authority**. By the time a job arrives, `policy-engine` has
  decided, the browser has minted and spent what it decided, and the
  supervisor has admitted the job. This process's answer is only ever "I will
  run this" or a closed refusal.
- It does **not own the interpreter**.
  [`taffy-core/third_party/cpython`](../../../third_party/cpython/README.md)
  is the pinned, GN-built interpreter and the frozen module set —
  `taffy_frozen.h`, `taffy_stdlib_boot` and `taffy_python_tools` all come from
  there. This directory embeds it and adds no module of its own.
- It does **not choose its sandbox**. `kService` is pinned in the contract, at
  `taffy-core/contracts/tool-runtime/tool_runtime_service.mojom`, with the
  reason written beside it: reviewed pure Python over bytes the browser
  supplied needs no operating-system resource beyond computation.

## What it does

`PythonToolServiceImpl` implements the contract's `PythonToolService` root.
One fixed registered invocation per utility process. **No method accepts
source, a module, a path, a URL or argv**; the only variable bytes in the
whole interface are the bounded declarative input to a compiled entrypoint.

**Admission is exhaustive, and it happens before anything starts.** Runtime
kind, operation kind, tool id and version, an entrypoint this build can
honour, a job id within bounds, inline transport whose declared byte length
and SHA-256 match the bytes, exactly one output chunk, and every budget field
inside its ceiling: input at most 256 KiB, output at most 1 MiB, memory at
most 64 MiB, CPU at most five seconds, temporary bytes exactly zero. A job
that carries an argument or a resource this worker does not use — a local
model, an adapter, media in or out, a bulk pipe — is **refused rather than
ignored**, because a field nobody reads is a field nobody notices arriving. A
second `Start` while one is live is backpressure.

**The standard library is proved before it is used.** It arrives as a
descriptor the browser opened; the executor memory-maps it and refuses unless
its length and its SHA-256 equal what the job declared.

**One interpreter, started isolated and finalized before returning.**
`PyConfig_InitIsolatedConfig` with no site import, no user site directory, no
bytecode written, no signal handlers, no environment, `safe_path` set and a
fixed hash seed. Nothing about the host's Python, if it has one, can reach
inside.

**The memory budget is the allocator.** `PyMem_SetAllocator` installs a
header-tagged counting allocator over all three domains, so exceeding the
budget is a `MemoryError` at the allocation that crossed it rather than an
out-of-memory kill some time later.

**The CPU budget and cancellation are the same trace hook.**
`PyEval_SetTrace` checks a cancellation flag and thread CPU ticks and raises
`KeyboardInterrupt` or `TimeoutError`; `CurrentFailure` maps whatever was
raised onto one closed terminal status, and the terminal carries no error
body. The cancellation flag is reference-counted because cancelling closes
the service pipe while the interpreter sequence may still be unwinding.

## The trap worth knowing

**This worker names its admissible entrypoints as literals.** `IsEntrypoint`
in `python_tool_service.cc` compares against `document.build` and
`spreadsheet.build` directly; it does not read the generated table. That is
the second of two independent gates working as intended — the core asks the
registry before it proposes a job, the browser asks the generated rows again
before it dispatches one, and neither takes the other's word — but it has a
consequence that is easy to miss: **a row added to the registry does not
become runnable here until this file names it too**, and until then the job is
refused as unsupported by a process that compiled perfectly.

## What this directory deliberately does not do

- **It does not queue or schedule.** There is no worker pool and no queue
  here; a queue would be a scheduler the browser cannot see, cancel or bound.
  Concurrency is the browser launching several of these through
  `taffy-core/browser/profile_python_tool_launcher.{h,cc}`, under the
  supervisor's own ceiling.
- **It retains nothing.** Output goes back through the contract and lives in
  browser custody; a successful terminal records a lowercase SHA-256, a byte
  count and a chunk count, and nothing else.
- **It runs no code a person or a model supplied.** The entrypoint is an
  identity resolved against compiled-in dispatch. There is no evaluator, no
  import of anything outside the verified library, and no writable path.
- **It is not the native path for work the core already owns.** Reshaping
  rows is refused to a worker by name and answered by
  [`table-engine`](../../../components/intelligence/core/rust/table-engine/README.md)
  inside the sandboxed core; producing document bytes natively is
  [`file-engine`](../../../components/intelligence/core/rust/file-engine/README.md)'s.
  Starting a process for something the product already does buys nothing and
  costs a process, a budget, an opened resource and an attack surface.
- **It is not built everywhere.** Python is compiled only where
  `taffy_python_runtime_in_current_toolchain` selects the source-built
  interpreter; the diagnostic host profile keeps it false, because that
  profile cannot truthfully run the Android interpreter, and the supervisor's
  port answers `Unsupported()` there.

## Commands

Builder only — nothing on a host compiles this directory:

```bash
./tools/chromium/build --profile dev-arm64 taffy_browsertests
out/dev-arm64/bin/run_taffy_browsertests \
  --gtest_filter=PythonToolWorkerBrowserTest.RegisteredBuildersSurviveWorkerResultConsumptionInBrowserCustody
./tools/chromium/test --profile dev-arm64 --device <serial> \
  taffy_browsertests -- \
  --gtest_filter=PythonToolWorkerSp07BrowserTest.SandboxProbeHasNoAmbientAuthority
./tools/chromium/test --profile dev-arm64 --device <serial> \
  taffy_browsertests -- \
  --gtest_filter=PythonToolWorkerSp07BrowserTest.CpuAndMemoryLimitsEndOnlyTheirWorker
./tools/chromium/test --profile dev-arm64 --device <serial> \
  taffy_browsertests -- \
  --gtest_filter=PythonToolWorkerSp07BrowserTest.CancellationKillsAdmittedWorkerAndRecovers
```

Name the browser tests you run; never hand a wildcard to the device runner.
[`taffy-core/test`](../../../test/README.md) explains why: the run reports
fewer tests than the suite has, which reads like a small suite rather than a
broken one.
