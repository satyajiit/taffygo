# Core API

Core API is the portable UI contract. Compose and future desktop clients send
user or platform intent and receive immutable view state. It exposes no policy
grant, browser capability, raw effect, provider response, or page graph.

Page Inspector records are a separate bounded UI projection, not a BIP
binding. Raw renderer snapshots, BIP enums, encoded graph bytes, capability
references, approval digests and full URLs terminate in the trusted
browser/content layer. The generated Core API exposes only typed availability,
document metadata with an approved host field, coarse claims/warnings/budgets,
and a redacted snapshot whose identifiers are freshly allocated for UI use.
Kotlin and future desktop TypeScript consume the same records and limits.

Android sends only user account intents and permission results through this
contract. Authorization callbacks bypass UI semantics: Android receives the
raw platform intent, the profile browser broker validates and seals it, and a
browser-internal Core Service command delivers the typed receipt to Rust.

`schema/contract.json` also owns the versioned `CoreStatus` payload layout.
Its generator emits Rust, Kotlin, and TypeScript codecs over the same generated
records, plus one cross-language golden payload. Decoders reject unknown enum
values, unsupported versions, oversized fields or collections, malformed
presence combinations, truncation, and trailing bytes. Consumers must not add
a second hand-written codec.

`golden/full-status-v1.hex` is an **input**, not generated output. It used to be
written by `--write` and then compared against the same expression on `--check`,
so a change to the wire layout moved both sides together and the gate stayed
green for exactly the change it existed to catch. It is now read and compared
only. `--refreeze` is the one way to replace it, and it refuses unless the
codec's `schema_version` and the contract's `version` were both raised in the
same change. `schema/wire-ledger.json` is what remembers the versions it was
frozen at, and every ordinal beside them.

`compat/` carries real bytes and real documents. Every payload fixture is
decoded by the generated Rust decoder and by the generator's reference reader,
and both must reach the verdict and the named decoder error. The generated
Kotlin and TypeScript codecs are checked for staleness and are **not** executed
against the corpus; running Kotlin needs a compiler and a JVM this generator may
not assume. See `taffy-core/contracts/README.md` for what each fixture kind
executes.

The `core-status-*.hex` fixtures are **derived, not typed**. Each one is the
same accepted payload plus exactly the deviation its manifest row describes, and
`codegen/status_payload_corpus.py` is what cuts them:

```bash
python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --check
python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --write
```

`--check` proves the committed bytes are exactly what the schema derives, so a
fixture edited by hand does not survive. Raising the codec's `schema_version`
means running `--write`, because every one of those payloads carries the version
word and all twelve go stale together. They did once: the corpus sat at version
6 while the codec wrote 7, so the fixture whose whole job was to prove a current
payload decodes was proving the opposite.

Run the generator separately with `--check`, `--self-test`, and `--verify`:

```bash
python3 taffy-core/contracts/core-api/codegen/generate.py --check
python3 taffy-core/contracts/core-api/codegen/generate.py --self-test
python3 taffy-core/contracts/core-api/codegen/generate.py --verify
```
