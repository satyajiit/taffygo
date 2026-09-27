# Profile tool supervisor

[Current] This directory is the browser-side bridge between a profile's Core
Service effect broker and isolated runtime-specific workers. It translates the
generated Core Service and Tool Runtime objects field by field, owns one
`ToolRuntimeClient` endpoint for each admitted worker job, and resolves every
accepted Core Service request exactly once.

The supervisor has three independent compile-time launch/cancel ports:
bundled Python, a local model, and media. Product composition supplies those
ports only when that runtime's milestone and security gate is complete.
Shipping Android composition supplies **two of the three**:
`taffy-core/browser/profile_media_tool_launcher.{h,cc}` and
`profile_python_tool_launcher.{h,cc}` each start one sandboxed process per
job. Python is compiled only when `taffy_enable_python_runtime` selected the
source-built interpreter; configurations without it retain the same typed
`Unsupported()` port. Local-model composition instead always supplies
`ProfileLocalModelToolLauncher`. Its default build has no runtime adapter and
therefore answers `LOCAL_RUNTIME_UNAVAILABLE` without launching a process. The
resource binder answers `MODEL_ARTIFACT_MISSING` before that when the exact
signed catalog revision is not registered, and
`MODEL_ARTIFACT_INCOMPATIBLE` when registered facts disagree or a future
compiled adapter does not support their closed format. None of the three
answers carries a path or catalog body. No on-device runtime has been selected;
SP-08 still owns that choice and the launcher accepts only the narrow ports of
the selected sandbox service when one exists.

There is no universal worker, generic executable payload, runtime fallback, or
worker implementation here. WASM remains a reserved contract value and is
always refused as unsupported.

Per-profile admission is bounded. The supervisor checks generation, deadline,
job identity, idempotency identity, runtime/operation pairing, resource
budgets, sequence order, chunk totals, completion shape, and disconnects. A
failure produces a closed terminal status without an error body. Python and
media launch ports are intended to create one process per job; the local-model
port may address a warm profile-partitioned service while conversations remain
disposable.

`ProfileToolSupervisor::Start` is the only Core Service-facing entry point.
The product composition edge binds it to `CoreEffectBroker::Handlers::tool`;
neither side receives a generic process launcher or a runtime library type.

`CancelTask` is the other half of that binding, and the reason the binding is
safe to make. A tool job carries the task that owns it, so a cancelled task
kills its own workers and nothing else — generation cancellation cannot stand
in for that, because a generation ends when the core does and one cancelled
task leaves the rest of a profile running. A job whose effect named no task is
never guessed into the set: an empty owner means the request was made outside
any task, and claiming it would stop work a person is still waiting on.

## Resources, handles and streams

A worker is started with a job and with `ToolJobResources` — the opened kernel
objects the job's declarations describe — and with nothing else. Three pieces
here produce them, and each fails closed:

- `tool_handle_broker.{h,cc}` mints an opaque identifier bound to one job and
  one mode, answers whether an identifier was minted for that exact pairing,
  resolves one into an open descriptor, and revokes on job end, generation
  change and shutdown. A worker's identifier is compared, never dereferenced.
- `tool_job_resources.{h,cc}` decides the input and output transports from the
  declared length and the operation, opens the registered model artifact
  through an injected register port and requires the job's length, digest and
  format to match exactly, opens the bundled Python standard
  library through a second injected port and does the same to its declaration,
  moves bulk input into a read-only shared memory region, and resolves the
  media handles.
- `profile_tool_supervisor.{h,cc}` forwards a streamed chunk as it arrives
  rather than buffering it, counts it in the terminal result instead of
  carrying it twice, and refuses a chunk that arrives after the one marked
  final.

Every resource port is injected, for the same reason the launch ports are:
opening a file is ownership this directory does not have. `taffy-core/browser/`
holds the three implementations: `ProfileModelRegister` for model artifacts,
`ProfileToolHandleStore` for one-job media descriptors, and
`ProfilePythonLibrary` for the exact installed standard-library archive.
`CoreServiceManagerFactory` installs the applicable ports for every profile.
`ProfileToolArtifactBroker` admits only a completed download already attributed
to the same task, creates bounded private output files for transforms, and
spends fresh read/write handles when the supervisor binds the job. The model
register consumes decision 0101's complete installed-catalog snapshot, hashes
each exact opened revision off the UI sequence, and resolves only a matching
read-only descriptor. `tool_job_resources::Bind` then compares the job's kind,
format, role, length and digest exactly; it never overwrites a disagreement
into a different job. The product catalog publishes no model row yet, so the
shipping register is empty without treating that absence as a missing
manifest.

No job is planned to stream until a stream sink exists — a transport whose
other end is absent is never selected. `DATA_PIPE` is the same rule written
twice: `tool_job_resources.cc` never selects it, because the browser side of a
pipe is a drain with its own bounds and failure modes and nothing here is one,
and `profile_tool_supervisor.cc` refuses a job whose plan names it anyway. The
second is a backstop for a future plan, not a reachable path today, and
`ThePlanNeverSelectsTheTransportWithNoDrain` is what keeps it that way.

The Python path now supplies both halves. The source-built interpreter is part
of the APK, while the required `python-stdlib` artifact is installed by the
delivery plane. `ProfilePythonLibrary` opens and hashes the current installed
revision off the UI sequence, caches only the descriptor and measured facts,
and refreshes when the installed set changes. `ToolPythonLibrary` and its
descriptor must agree exactly before a worker starts. The interpreter then
boots with an empty import path and serves imports from the mapped descriptor;
the worker never learns a path and cannot open a replacement archive. A build
without the interpreter installs neither launch nor library port and reports
the runtime as unsupported before spending a job.

Decision `docs/decisions/0041-tool-runtime-carries-opened-resources.md` is the
record.
