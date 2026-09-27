# Browsing

The browsing seam: tabs, navigation and downloads, as anything drawing a
browser window needs them.

It carries what the browser process owns and a surface merely renders — which
tabs exist, which one is selected, what it is showing, why a page did not
arrive, what is downloading and how far it has got — plus the commands a
person may issue about those things. It carries no browser object, no full
location, no capability and no page content. A host is what a person is shown
and what a switcher needs; a full location is a credential-bearing string
often enough that this seam refuses to be the thing that carries one.

Address-bar interpretation is deliberately absent. Deciding whether typed text
is a location, a search, a question or a task is a product rule about a
surface's own input box, not something the browser answers, and putting it
here would make one seam responsible for two different questions.

## What it projects, and what it does not

`schema/contract.json` declares `projections`, and this contract emits four of
the five:

| Projection | Why it exists |
|---|---|
| `mojom` | The seam itself. `//taffy/contracts/browsing:browsing_types` compiles it, and the C++ records the browser fills come from it. |
| `cpp` | The closed-enumeration decoders. `//taffy/browser:browsing_projection` is the consumer, and `browsing_projection_unittest.cc` runs them. |
| `rust` | Executed by the compatibility corpus. `--verify` compiles the generated Rust with one `rustc` invocation and asks it what each fixture decodes to, which is how "unknown values fail closed" is a result here rather than a sentence. |
| `typescript` | The surface on-ramp, compiled by `taffy-core/contracts/codegen/typescript_typecheck.py` under the union of this repository's TypeScript settings. |

**There is no Kotlin projection and no `generate_java`.** The Android surface
drives the browser through an in-process Kotlin seam
(`taffy-core/ui/android/core/browser/`), so a generated Java transport would
be a transport nothing sends on and three thousand lines of generated Kotlin
no target compiles — which is the defect this repository has already paid for
once. Decision
0043
section 4 records when it arrives.

## What version 1.1 added

The filtering plane of decision
0076
rides this seam, because a blocked request is a browser fact about a page a
person is looking at. `NavigationView` gained `filtering_active` and
`blocked_request_count` — the count is coalesced by `filtering.browser`, so
it may jump but never lie, and it resets when a navigation commits. The
surface gained `SetFilteringEnabled` and `SetSiteFilteringException`; the
exception names a **host, never a location**, for the same reason nothing
else on this seam carries one. Both are a person configuring their own
browser: no assistant authority moves, so neither touches the core service.

## What holds the rule this contract cannot express

The contract's shape rules describe records one at a time. One rule about a
snapshot needs two of them at once: exactly one tab carries `selected`, and
the navigation beside the list is that tab's. `compat/manifest.json` carries
it as an `unexecuted` row — counted separately and never as covered — and
`taffy-core/browser/browsing_projection.h`'s `StateIsCoherent` is what
actually holds it, because the browser is the only party with both halves.

## Commands

```bash
python3 taffy-core/contracts/browsing/codegen/generate.py --check
python3 taffy-core/contracts/browsing/codegen/generate.py --self-test
python3 taffy-core/contracts/browsing/codegen/generate.py --verify
```

Regenerate with `--write`. Generated files are committed and never edited by
hand. An append is recorded with `--refreeze`, which refuses unless the
contract's `version` was raised in the same change; see the frozen wire ledger
section of the [contracts README](../README.md).
