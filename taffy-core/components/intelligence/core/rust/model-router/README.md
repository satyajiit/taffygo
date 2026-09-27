# model-router

**Status:** `[Proposed]` — implements a contract that decision
0005 fixes for
the two routes and that the
provider registry and model catalog
specifies for everything else. That document is itself `[Proposed]` pending
spike SP-06 evidence, so the shapes here can still move.

**Implementation status:** `[Current]` This crate is a planner, a set of typed
contracts, and the five protocol families as one data table. It contains no
network code, no credential storage, and no provider list. Nothing here sends a
request.

Owning milestone: M3 (the first workflow). Authoritative specifications:
provider registry and model catalog,
decision 0005,
and the cost model.

## The authority boundary

The router answers three questions: which model, over which route, and what the
user is told about it. It answers nothing else.

It is not an authority. A catalog entry authorizes nothing by itself — every
side effect still crosses the policy and capability broker in `policy-engine`,
and a provider appearing in a catalog does not make a request to it permitted.

It owns no secrets. `credential::CredentialRef` is metadata and appears in
plans, disclosures, and audit records. `credential::SecretMaterial` is the
secret, has no serde implementation, prints a fixed marker, and never enters any
of them. The router says which header names an adapter will fill; the adapter
fetches the value from the secret store when it builds the request.

It performs no request. `wire` shapes a body and reads a reply — the families
are rows of one table keyed by a closed enumeration, so a family without a row
does not compile rather than falling through to whichever row was written
first. A body may replay an earlier tool call and the result it was given,
which is several genuinely different documents and therefore several more
fields of the same table; it is not a normalization layer, and a transcript
that has gone wrong is refused rather than repaired. The browser process holds the network stack and the credential
store and makes the call (decision
0052).
There is no URL, no header, no client, no executor and no clock anywhere in it.

It holds no page content, and that is a property of the signatures rather than
a claim about the code inside them. Every function that touches page-derived
material takes a borrowed view — `&str`, `&[..]`, `&JsonValue` — the body
writer appends into a buffer the caller owns, and every content-bearing field
of a reply reading is a `&str` into the caller's own value. No type in `wire`
has an owned field carrying content, so a reading cannot outlive the buffer it
came from.

It reads no clock and draws no random number. Catalog generation times arrive as
data, deadlines arrive as monotonic values the caller supplies, and retry jitter
is injected through a trait. A routing decision is reproducible from an audit
record, which is what makes one worth writing.

## What the code is shaped around

| Rule | Where it lives |
|---|---|
| A route never changes under a failure | `route::RouteSelector` returns a refusal. It was written so that no path led from a failed credential to the managed route; decision 0200 removed that route, and the rule now binds on the substitution that remains — one provider for another |
| Failover stays inside one disclosure class | candidates are filtered to the primary's class before a plan exists, and `RoutePlan::crosses_disclosure_boundary` proves it |
| Nothing fails open | unknown provider, unknown model, unknown enumeration value, unknown schema version, absent kill switch, and absent credential are all refusals |
| Ordering is total | assistant configuration first, then catalog key order; no hash iteration anywhere |
| Prohibited material never leaves | refused before route eligibility is even considered |
| A model is never handed tools it cannot answer with | `Model::tool_calling` is required of every catalog entry, checked by `catalog::validate` on every merge layer, refused by `route::RouteSelector`, and refused again by `wire::write_request` |
| Retries and failovers are part of a task's cost | one `cost::TaskLedger` per task, and every call kind spends from it |

## The catalog

`catalog/baseline.json` is the catalog compiled into the build, and it is
**generated**: the facts live once in `catalog/source/`, in the units the vendor
publishes them in, and `catalog/tools/generate_baseline.py` is the only thing
that turns them into the shape this crate decodes. `./tools/check fast --only
catalog` fails a tree where the two disagree, which is what decision
0015 asks of a
stale embedded baseline. [`catalog/README.md`](catalog/README.md) is the
authority for that directory: what the generator converts, what it refuses, and
why the invariants stay in `catalog::validate` rather than in it.

The baseline carries the launch provider set decision
0029 decided. It
also carried the managed roster that decision
0028
metered in credits; decision
0200
removed the route that roster served and superseded 0028 whole, so the roster
and the meter are both gone. The person's own configuration merges over the
baseline. A served overlay merged between them until 0200 deleted the host that
served it and the command that published to it; `MergedCatalog::build` keeps the
argument and is passed `None`, so the layer order here is still the order the
merge implements.
Four tests in `tests/catalog_document.rs` hold the decisions to it: the provider
list is the decided one, one descriptor per vendor with the class still derived
from the credential, the provider whose terms forbid third-party use ships with
its kill switch off, and every role resolves to at least one enabled model.

Rich catalogs that exercise every field — both protocol families, both auth
methods, metered and imputed pricing, a long-context tier, a narrowed thinking
ladder, and both kill switches — live in `tests/common/mod.rs`, where "fixture"
already means synthetic.

## Dependencies

`serde` only, plus `serde_json` for tests. Every shipping crate clears
Chromium's vendoring review, so the runtime dependency tree stays near zero by
design (decision
0004).

**Gap:** the crate reads catalog documents with its own bounded reader in
`src/json.rs` rather than a general-purpose JSON library, because the obvious
candidate would be a second runtime dependency. The catalog types still derive
serde traits, and `the_reader_and_serde_agree_on_every_committed_document`
decodes every committed document both ways and compares, so the hand-written
reader cannot drift from the serde shape without failing a test. If the
vendoring review later admits a JSON library, that reader is the first thing to
delete.

## Numbers

The tuning defaults of this layer — thinking budgets, the reserved answer
allowance, refresh and backoff timings, retry attempts, catalog refresh cadence
— live in `src/defaults.rs` and nowhere else in the crate. The
provider registry and model catalog
document owns those values; that module tracks it. None of them is a quality
target: those live in the metric registry and
are not restated in code.

## Verifying

```bash
cargo fmt --all
cargo clippy -p model-router --all-targets -- -D warnings
cargo test -p model-router --locked
```

| Test file | Proves |
|---|---|
| `tests/catalog_document.rs` | For any document, whatever layer it arrived on: a bad entry drops itself and not the document; unknown providers and models fail closed; a staler overlay never wins; the hand-written reader and serde agree |
| `tests/embedded_baseline.rs` | For the document this build ships: it parses and satisfies every invariant, its provider list is the one decision 0029 decided, one descriptor per vendor with the class still derived, the vendor whose terms forbid third-party use ships shut, and every role resolves inside the managed roster |
| `tests/route_selection.rs` | The eligibility matrix; a broken credential refuses instead of reaching managed access; failover order is stable and never leaves its disclosure class; quota and budget refuse before a call happens |
| `tests/secrets_and_cost.rs` | A secret appears in no plan, disclosure, report, or debug output; pricing rounds up and selects tiers; a budget refuses the call that would pass it; imputed spend is reported as imputed |
| `tests/thinking_and_retry.rs` | Ladder clamping searches up then down; a budget-mapped family always leaves room for an answer; unretryable classes are classified first; a request ends exactly once |
| `tests/tool_calling.rs` | The one capability fact at all four of its gates: an entry omitting it is dropped, a task role without it is a violation on every merge layer, the selector refuses such a model for a call carrying tools, and the body writer refuses to write them |
| `tests/wire_request.rs` | Each row places its fields where that family puts them; the assistant is named by the family; the standing instruction is a turn in exactly one of them; a nested allowance and a nested budget share one parent object rather than emitting it twice |
| `tests/wire_tool_golden.rs` | The exact bytes each family is sent for one tool exchange, against the committed documents in `tests/golden/` |
| `tests/wire_tool_cache.rs` | Short and long retention mark only the final tool on Anthropic Messages; unsupported families and empty tool lists omit explicit cache fields |
| `tests/wire_tool_replay.rs` | The identity the core minted reaches every family that has room for one, unaltered; the family with room for none pairs a result to its call by the tool's name; a failure reaches the wire in whatever way each family has; arguments are escaped into the body rather than spliced into it; no view on the write path can hold an owned `String` |
| `tests/wire_tool_refusals.rs` | An unpaired call or result is refused rather than synthesized; a turn replaying nothing, or more calls than a reply may carry, or arguments nesting past the reader's own bound, is refused; a replayed exchange needs a model that can answer with a tool call; every refusal leaves the caller's buffer as it found it |
| `tests/wire_reply.rs` | The prompt total is the same number on every family however each one folds its cached tokens; a tool call is one whatever the family calls the stop; one family mints no identity; a reading borrows from the reply rather than copying it; the three overflows that look like successes are not read as successes; a null where a failure or a count would be is read as the absence it is |
| `tests/subscription_reach.rs` | One descriptor per vendor: a key reaches the key family at the key address and a subscription reaches the other pair, while a row with one way in answers the same either way; the subscription responses body carries what the platform endpoint refuses; sealed reasoning comes back out of a reply and goes back in unchanged and can be neither printed nor recorded; the caller-identifying preamble is absent on a key, present exactly once on a subscription, and refused by a family with nowhere to put it |
| `tests/server_compat.rs` | A detected server is told the answer allowance under the name it knows and is not sent a field it may refuse the request over; chat-template arguments and sampling parameters reach the body of the one dialect that has a place for them; a server that never says why it stopped is readable, and one that does is still refused when it does not |
| `tests/managed_wire.rs` | The managed schema's own writer and reader: the body is the worker validator's shape field by field; a whole tool exchange round-trips at version 2 with the identities the core minted; a managed transcript and a transcript written through a dialect walk the same steps with the same identities; every refusal is the direct writer's own assertion and leaves the caller's buffer as it found it; the answer's stop is read from the calls in it rather than from the word beside them; a version this build did not write is refused by name |

## What is not here yet

- Recorded provider fixtures. `tests/wire_request.rs`, `tests/wire_reply.rs`
  and the documents in `tests/golden/` prove the table places a field
  where the family puts it; they do not prove a real provider accepts the
  result, which is the conformance suite the registry document specifies. They
  are hand-written published shapes, and a hand-written shape is exactly where
  a field the provider always sends can go missing — `"error": null` did, and
  the reader read its absence as success on the one path that matters most.
- Streaming. Decision
  0052
  leaves it out of the first version rather than answering by implementation
  what a partial reply means to a record that says what happened.
- Separate prompt-cache resources and conversation-cache markers. The direct
  writer supports optional tool-prefix retention on Anthropic Messages;
  `loop-kernel` enables short retention only for compiled declarations sent to
  the catalog Anthropic service. The default marker uses the documented
  five-minute lifetime; an explicitly requested long class writes `ttl: 1h`.
  System instructions, retrieved content, page data and transcript blocks
  receive no explicit marker. Other families and custom endpoints omit it.
  [Anthropic's tool-cache specification](https://platform.claude.com/docs/en/build-with-claude/prompt-caching#caching-tool-definitions)
  defines this prefix as tools only. Host tests prove body placement and the
  running composition's selection, not a live cache hit or latency improvement;
  the provider's minimum prefix length and usage report decide whether it hit.
- Carrying a family's sealed reasoning *across* turns. `wire` reads it out of a
  reply and writes it back into the next request, both through
  `OpaqueReasoning`, which borrows — so the round trip holds only while the
  caller still holds the reply. Keeping it for a later turn means storing it,
  and what a durable record may hold is not this crate's to decide.
- The credential store and the authorization flows; this crate consumes
  credential metadata through `credential::CredentialDirectory`.
- The served catalog fetch and its on-device cache, which belong to the browser
  network and storage brokers and to the catalog publishing design (OD-074).
- On-device generation, which is a planned third route behind its own open
  decision (OD-037) and has no variant in `route::Route` until it exists.

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../AGENTS.md#documentation-conventions).
