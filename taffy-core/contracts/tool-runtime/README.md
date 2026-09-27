# Tool Runtime contract

This contract is the only broker-to-worker wire boundary. Every job selects a
closed operation with exactly one generated argument record. Workers can emit
bounded ordered chunks and exactly one typed terminal result; opaque catch-all
argument and output payloads are not part of version 2.

Each runtime has a separate service and dependency closure. Python and media
workers are one process per job. A local-model service may remain warm only for
one profile. The WASI shapes reserve compatibility space but no WASI service is
built. No worker receives an arbitrary path, URL, command line, executable
source, browser authority reference, or provider credential. Media handles are
opaque broker-owned identifiers, never paths.

Python modules and runtime code are bundled with the signed application; the
wire names a reviewed module, version and allowlisted entrypoint. LiteRT-LM
remains an adapter candidate, not a dependency. Media code reuses Chromium's
pinned media and FFmpeg targets. Runtime-specific implementations are added at
their owning milestones rather than as empty or permissive fallbacks.

## Declarations here, resources beside them

The generated schema carries what a job *declares*: a registered model identity
and revision, an artifact format, a length, a SHA-256 digest, an input and an
output transport. It carries no kernel object, and it never will: this schema is
projected into Rust, Kotlin, TypeScript and Mojo from one source and its
ordinals are frozen in `schema/wire-ledger.json`, and a file descriptor has no
honest projection in three of those four.

The opened resources those declarations describe travel beside the job, in
`ToolJobResources` in the hand-written `tool_runtime_service.mojom`. That is
where a read-only model artifact, an adapter, a read-only shared memory region,
a media input, a media output and a bulk-output pipe live. The split is the
security property rather than a layout convenience: a name in a message is
something a sender can write, and an open descriptor is something only the
browser can produce.

Three rules follow, and each is enforced in
`taffy-core/services/tool-runtime/supervisor/`:

- **A model is resolved, never named into existence.** The browser looks the
  identity up in its own register, opens the artifact, and requires the
  outgoing job's length, digest, format and role to match what the register
  recorded exactly. A disagreement is refused as
  `MODEL_ARTIFACT_INCOMPATIBLE`; it is never repaired into a different job.
- **Bulk input above `MAX_INLINE_PAYLOAD_BYTES` leaves the message.** The
  browser maps a read-only region, copies the bytes in, and clears the inline
  field. The two carriers are alternatives, never a redundant pair.
- **An opaque handle carries no authority.** `ToolHandleBroker` mints it,
  binds it to one job and one mode, resolves it browser-side into a descriptor,
  and revokes it when the job ends. A worker's handle is compared, never
  dereferenced.

`DATA_PIPE` is a declared output transport this broker does not yet select, in
the same way the WASI family is a declared runtime it does not yet start. The
browser side of a pipe is a drain with its own bounds; selecting the transport
before that exists would hand a worker a producer nobody reads.

Decision `docs/decisions/0041-tool-runtime-carries-opened-resources.md` records
all of it, including why the local-model service pins
`kOnDeviceModelExecution` and the media service does not.

## What the compatibility corpus proves here

This contract declares no byte codec: jobs, chunks and completions cross the
seam as Mojo messages. `compat/` therefore has no payload fixtures and no
decoder run, and the corpus says so rather than implying otherwise.

What is executed is real and it is narrower than the other two contracts. A
closed-enumeration fixture is answered by the generated Rust `from_wire`, and a
bound fixture by the generated limit constant the product compiles in. Shape
rules — one argument record per job, the runtime/operation pairing, the terminal
status that carries a success record — are executed by the schema's own rules in
the generator, not by a shipped decoder. One fixture, the ordering rule over a
chunk stream, executes nothing at all; it carries a written reason and is
counted separately. `schema/wire-ledger.json` freezes every ordinal on this seam
under the same rules as the other two contracts.
