# Filtering

The ad- and tracker-blocking feature plane of decision
0076.

| Directory | What it is |
|---|---|
| `core/` | The pure decision policy: compile pinned rule-set text into an engine, answer block or allow for one request's facts, and answer whether a site posture leaves filtering active. No I/O, no browser object, no thread. |
| `browser/` | The performing half: browser-initiated request throttles, renderer-request decisions, per-tab counters and their coalescing, preference-backed posture, ruleset lifecycle from asset-store bytes to a published matcher, and CosmeticFilterHost. The engine and profile facts stay in the browser process. |
| `mojom/` | The narrow renderer-to-browser seams for hide stylesheets and resource decisions. The renderer holds each remote; the browser implements. |
| `renderer/` | Class/id observation, a user-origin hide stylesheet, and the loader adapter that defers renderer-originated resources while the browser decides. It receives only a block/allow result, never rules or profile state. |

The matching engine inside `core/` is Brave's `adblock-rust` (MPL-2.0),
reached through a C ABI — decision
0086,
which supersedes
0077
and re-resolves OD-111. The crate is vendored at
`taffy-core/third_party/adblock-rust/` and is not a TaffyGo workspace
member. Cosmetic syntax the engine accepts is indexed, not skipped. The
rule sets are the pinned snapshots under
`taffy-core/third_party/easylist/`, delivered as the `easylist-base` asset — a
required part the delivery plane fetches on its own on any live connection,
with the copy in the APK read until it lands; no screen offers it for
download, and SCR-206 only reports its state.
A blocked request is never a task fact: nothing in this plane touches the
journal, the Core API, or the AI data plane. Ad and tracker blocking is free
for everyone, and there is no longer any other possibility: decision
0028 reached
that answer by its marginal-cost test, and decision
0200 superseded
0028 by leaving nothing that could be metered and no account to meter it
against.
