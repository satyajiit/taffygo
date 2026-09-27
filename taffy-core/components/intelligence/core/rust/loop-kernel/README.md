# loop-kernel

**Status:** `[Proposed]` implementation of decision
0072,
which is accepted at the M3 exit review together with decision
0052.
**Implementation status:** `[Current]` the walk, the turn composer and reader,
the person-answer classification, the one transient-state owner, the bounded
page and conversation arena, the context-window plan and the narrow ports all
exist, and their suites run under `cargo test -p loop-kernel --locked`. That
is a host result over pure functions. The crate reaches a device only compiled
into the sandboxed core service, and the evidence for that is the Chromium
build's rather than this crate's.
**Owning work package:**
WP-M3-01
— the loop kernel. **Owning milestone:** M3.

## Authority boundary

This crate owns the machinery **around** the durable reducer, and nothing
inside it. The scheduler walk, model-turn composition and reading, the
person-answer classification, the transient state one task keeps between
commands, and the bounded arena a page and a conversation live in while a turn
needs them — those are here. The reducer itself stays in `task-engine`,
including the assistant decision table, which is a `&self` method over durable
state: extracting it would mean a second read-only copy of that state wearing
a module boundary.

**It grants nothing and performs nothing.** No capability is minted here, no
origin scope is widened, and no effect is executed. `policy-engine` still
decides every step and the browser still spends what it decided. A model call
leaves as a composed request the browser sends; the kernel holds no socket, no
clock, no file, no credential and no ambient state of any kind. Everything is
pure given its arguments, `now_monotonic_ms` included, which arrives as one
rather than being read.

Its GN visibility names `core-runtime` and nothing else, so no surface, no
service bridge and no other component can reach around the runtime and drive
the walk directly. The bridge under `taffy-core/services/core/` is transport
around `walk::advance`: it assembles the response, begins the submit and rolls
a staged ask back, and it makes no loop decision, because a decision taken in
a crate no host command compiles is a decision no host test has ever checked.

## What is here

| Module | Owns |
|---|---|
| `walk/` | The one walk. `advance` answers a gap, a planned command with its operation envelope, a wait, or an idle — in a fixed order: the refused model call settles first, loop-local calls settle on the residency next, then the consented route's own scheduler answers and the replay-stable envelope identity is minted |
| `turn/` | One model turn: composed from durable facts, and read back into a residency. `person_answer` classifies what a person said in reply |
| `recording/` | A fresh errand's bounded, browser-verified descriptions from its reviewed public starting address, including person hand-back; no transcript, field values or live control identities |
| `state.rs` | `LoopState`: everything the loop keeps between commands, with one owner and one drop point |
| `context/` | The arena. What one observed page and one conversation look like while the core still holds them — owned, bounded, and dropped when the turn ends |
| `window/` | The context-budget decision the walk reads: whether the conversation fits, must durably shrink first, or is at the ladder's honest end. The eviction *mechanics* stay in `context/transcript` |
| `ports.rs` | The narrow traits the kernel consumes, implemented above by `core-runtime`'s existing ports |
| `provider.rs` | The kernel's whole view of the provider plane: one opaque `CredentialHandle` and the single question the turn composer asks about it |
| `digest.rs` | The one cryptographic primitive the kernel is lent. Production strength is Chromium/BoringSSL's; this states the shape |
| `native_table.rs` | The narrow adapter from one transient model call to `table-engine`. Bounded typed operands in, one transient result out, no process started and no byte journalled |

## What this crate deliberately does not do

- **It does not decide.** The durable reducer and its decision table are
  `task-engine`'s. This crate asks what is next and carries the answer.
- **It stores nothing.** There is no database handle, no journal writer and no
  path. Durability is `STORAGE_COMMIT`, decided above and committed by the
  browser.
- **It reads no clock and opens no socket.** Chromium owns tasks, clocks,
  cancellation and shutdown; effects are performed above and return as typed
  completions. There is no async runtime here.
- **There is deliberately no port for `LoopState`.** A port claims
  substitutability, and the property being protected is that a restore
  constructs this state *empty* — that is the structural proof that nothing
  transient survives a generation. One owner, one drop.
- **Page and conversation arenas never become durable.** The recorder may
  export a bounded sequence of compiled verbs, semantic phrases and one
  reviewed public starting address through `core-runtime`'s inactive skill
  writer. It cannot export either arena, a personal field value or a live
  control identity. See 0139.
- **It holds no credential.** A `CredentialHandle` is the whole of what the
  core is told about a key, and its `Debug` is empty on purpose: a handle is
  not secret, but an accidental appearance in a log would read as one and send
  somebody looking for a leak that did not happen.
- **It starts no worker.** `native_table` is in-process arithmetic in the
  sandboxed core, which is exactly why the entrypoint registry refuses
  `table.reshape` to a sandboxed interpreter and names the native owner
  instead (decision
  0064).

## Independent reads and accepted replay

The pre-model walk can prepare at most four independent whole-page reads over
consented tabs. Every read still goes through the reducer and ordinary policy.
Their source order determines model composition after all terminals settle;
mutations, handover and model-requested sequences keep their ordering. The
native service must also dispatch those independent reads concurrently: a
portable scheduling test alone cannot prove that property.

A selected active procedure takes a separate deterministic path. It observes
again after navigation, plan progress and hand-back, resolves a unique current
semantic control and submits an ordinary action. A fresh-link download uses
the current handle and browser session; only owned complete transfers satisfy
the replay's terminal download witness. There is no model call on this path.

## Commands

```bash
cargo test -p loop-kernel --locked
./tools/check fast --only rust
```

Cargo is the fast host loop. GN is authoritative for what ships:
`//taffy/components/intelligence/core/rust/loop-kernel:loop_kernel`, built
inside `./tools/chromium/build` and linked into the sandboxed core service
described by [`taffy-core/services/core`](../../../../../services/core/README.md).
