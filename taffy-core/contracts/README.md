# Taffy cross-process contracts

This directory owns five distinct trust seams. The Browser Intelligence
Protocol remains the renderer/page contract. `browsing` is the tab, navigation
and download seam any surface drawing a browser window drives, `core-api` is
the platform UI facade for the assistant, `core-service` is the
browser-to-Rust utility protocol, and `tool-runtime` is the browser
broker-to-worker protocol.

The four contracts here use one deliberately small standard-library generator
to emit Rust, Kotlin, TypeScript, C++, and Mojo projections. Every schema uses
append-only ordinals and closed enums. Unknown enum values stop processing.

**A contract emits the projections it declares, and no others.**
`schema/contract.json` carries a `projections` list, and adding a language to
it is the same edit as wiring up the target that compiles the result. A
projection nothing compiles is a file that has never been through a compiler,
cannot fail, and reads in a review as though it had been checked — which is
the shape of defect this repository has already paid for once, in three
thousand lines of generated Kotlin that no target named.

An interface is a declaration kind like any other. A contract that declares
one carries a written ordinal on every method and every argument, because Mojo
assigns one by declaration order otherwise and a tidy-up of the list then
reinterprets every message a peer already sends. A handle reaches an interface
in a parameter and never in a reply — an interface is handed over, not
returned — and a record field is never a handle at all, because a record is
stored, copied and replayed and a connection survives none of it.

A method and the body record it carries are two independent lists, and nothing
in a schema makes them agree: a field appended to the body alone validates
perfectly and is unreachable, because no argument supplies it. That has
happened twice. `facade_bodies` in a contract's schema binds one method to one
body, naming the fields the browser mints rather than the caller and the one
place the two spell an argument differently, and
`codegen/facade_bodies.py` refuses a body field no argument
supplies, an argument no body field carries, a type that differs between them,
a tagged body no method claims, and an exemption that no longer names a field:

```bash
python3 taffy-core/contracts/codegen/facade_bodies.py
python3 taffy-core/contracts/codegen/facade_bodies.py --self-test
```

The C++ projection is a header of closed-enumeration decoders and nothing else
— `generated/cpp/<contract>_enums.h`, one
`std::optional<mojom::Enum> EnumFromWire(uint32_t)` per enumeration. It carries
no records, because the Mojo projection already gives C++ those. It exists
because C++ is the one language here where the unchecked conversion compiles:
`static_cast<Enum>(integer)` for a value that names no enumerator is undefined
behaviour, and at a trust seam it is a fail-open, since the value travels on
and every later `switch` misses it silently. Rust's `from_wire` and Kotlin's
`fromWire` had no C++ counterpart, so every C++ seam wrote the cast by hand.

```bash
python3 taffy-core/contracts/codegen/generate.py --check
python3 taffy-core/contracts/codegen/generate.py --verify
python3 taffy-core/contracts/codegen/generate.py --self-test
python3 taffy-core/contracts/codegen/cross_contracts.py
python3 taffy-core/contracts/codegen/cross_contracts.py --self-test
```

Regenerate with `--write`. Generated files are committed and never edited by
hand. BIP has its own mature generator and fixtures under `taffy-core/contracts/bip` until
that contract is physically moved into this root during the hard cutover.

Core Service and Tool Runtime intentionally project the same closed tool
operations at different process seams. `cross_contracts.py` makes their shared
enums, typed records, runtime/operation pairing, success rule, and payload or
resource limits one checked invariant while allowing each contract to retain
its own envelope and streaming shape.

## The frozen wire ledger

Append-only ordinals are necessary and they are not sufficient. Inserting a
field in the middle of a record and renumbering the rest satisfies contiguity
perfectly while changing what every following field means, and on the device
journal that means a phone decoding transactions it already holds under a
layout that no longer describes them.

So each contract carries `schema/wire-ledger.json`: every enumeration member,
every record field ordinal and type, and every tagged-union variant, recorded
once. The generator compares it against the schema on every run.

- **Reuse, reordering, retyping and removal are refused**, and no flag permits
  them. There is no change of that kind that is safe.
- A method's parameters are recorded **one entry per argument**, the way a
  record's fields are. Mojo builds a parameter list into a versioned struct, so
  appending an argument is the same append-only change as adding a field —
  while the ledger recorded a method as one signature string it could not tell
  an append from a rename, refused it outright, and left declaring a second
  call beside the first as the only way to give an existing call a new
  argument. A reply is still frozen whole, because nothing has yet needed to
  append to one.
- **An append is recorded only by `--refreeze`**, a sixth generator mode that
  belongs to the same mutually exclusive group as `--write`. It refuses unless
  the contract's `version` was raised in the same change.
- **A codec's `schema_version` must be raised** when the append lands inside a
  record that codec reads. That number is the only thing that lets an older
  decoder recognise a payload it does not speak.

This generalises the `x-bip-added-in` ledger BIP already carries. BIP records
*when* a member arrived, because its fixtures derive an older reader from it;
this records *where* a member sits, because these contracts are positional.

## Frozen payloads

A golden payload is evidence only if it is an input. Core API's `CoreStatus`
bytes and Core Service's `TaskTransactionBatch` bytes are read and compared,
never written by `--write`; `--refreeze` is the only way to replace them, and
it refuses unless that codec's `schema_version` was raised. Each is re-encoded
by two independent writers and decoded again, because re-encoding proves
today's writer still produces yesterday's bytes and decoding proves today's
reader still understands them.

## The compatibility corpus, and what it does not prove

`compat/manifest.json` in each contract carries real payloads and documents
rather than a table of names and verdicts. Every fixture declares its **kind**,
and the kind says which artifact reaches the verdict:

| Kind | What executes it |
|---|---|
| `payload` | The generated Rust decoder, over committed bytes, plus the generator's reference reader |
| `enum` | The generated Rust `from_wire`, over the value the document carries |
| `bound` | The generated limit constant the product compiles in |
| `version` | The contract version this build declares |
| `record` | This contract's own shape rules — tagged-union exactness, conditional presence, enum pairing, field completeness — in the generator |
| `declaration` | The schema validator, over a committed schema fragment it must refuse |
| `unexecuted` | Nothing. The row carries a written reason and is counted separately |

`--verify` prints the counts and says, in as many words, what ran. Three things
are worth stating plainly:

- **The generated Kotlin and TypeScript decoders are never run here.** Running
  Kotlin needs a Kotlin compiler and a JVM, which a stdlib-only Python
  generator may neither assume nor install. They are generated, committed and
  checked for staleness; they are not executed against the corpus.
- **The generated Rust decoder needs `rustc`.** When it is absent the run says
  so and executes the reference reader alone. It never reports a pass for
  something it did not run.
- **Tool Runtime has no byte codec**, so it has no `payload` fixtures. The
  generated Rust reached there is the closed-enumeration gate and the bound
  constants, not a decoder.

An `unexecuted` row is a requirement the contract states and no artifact here
can run — an ordering rule over a stream, or a comparison against state the
message does not carry. Those belong to the receiver, and the row says so
rather than implying the corpus tested them.
