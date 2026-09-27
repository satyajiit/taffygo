# The embedded catalog

**Status:** `[Current]` — `baseline.json` is generated from `source/` and the
`catalog` lane of `./tools/check fast` fails a tree where the two disagree.

This directory holds the catalog layer compiled into the release: the providers
and models a device routes against on first run and offline, before any served
overlay or user configuration reaches it. Decision
0015 makes that
data rather than kernel code and requires a build to fail on a stale embedded
baseline; decision
0029 decides which
providers are in it.

## The authority boundary

| Path | What it is | Who may edit it |
|---|---|---|
| `source/catalog.json` | The document header: schema version, catalog version, generation time, currency | By hand |
| `source/providers.json` | One record per provider, each naming the decision record that put it there | By hand |
| `source/models.json` | One record per model, in the units the vendor publishes, beside the page it was read from and the date it was read | By hand |
| `tools/generate_baseline.py` | The only thing that turns the source into the kernel's shape | By hand |
| `tools/check_vendor_agreement.py` | Compares a provider row's `OAUTH` offer against the three compiled tables a subscription sign-in also needs, in both directions | By hand |
| `baseline.json` | Generated. `../src/lib.rs` embeds it with `include_str!` | Nobody — regenerate it |

```bash
python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --write      # regenerate
python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --check      # fail if stale
python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --self-test  # check the rules
./tools/check fast --only catalog                                           # every catalog check
```

A provider row that offers `OAUTH` is not only catalog data. It is one of four
tables a subscription sign-in needs, and the other three are compiled — the
core's `SIGN_IN_VENDORS`, the Android flow map, and the browser's vendor table.
Those three agree with each other loudly; the catalog's disagreement with them
is silent in both directions, which is why `check_vendor_agreement.py` exists
and why adding a vendor here is never a change to this directory alone.

## What the generator decides, and what it refuses to

Two things it does are the reason it exists rather than being a copy step.

**Money is converted, never transcribed.** The source carries what the vendor
prints — `"0.44"` for a rate quoted as forty-four cents per million input
tokens — and the generator converts it to integer micro-units per million with
exact decimal arithmetic. A rate is never written twice in two units, so the
two cannot drift apart. A published amount with more than six decimal places is
refused rather than rounded: a rounded price is a price nobody published.

**A model that cannot call tools is not cataloged for a task role.** The source
records `tool_calling` per model, and the generator refuses to emit a model
that claims a planning, browsing, or image role without it. Retrieval support
is not a task role and calls nothing, so it is the one role the rule does not
apply to. The flag is also emitted into the baseline, because the generator
sees the embedded layer's source and nothing else: a served overlay and a
provider the user added themselves reach the router without ever passing
through this directory. `catalog::validate` checks the same rule on the merged
document, which is the one gate all three layers share, and the router refuses
to hand tools to a model whose descriptor says it cannot take them.

A third rule keeps the source honest about what it knows. Every field of a
model whose origin matters — both token limits, both capability flags, the
modalities, and all four rates — is named in exactly one of
`provenance.vendor_published` or `provenance.taffygo_chosen`, and the second
half carries a reason in prose. A number attributed to nobody is a finding, and
so is one attributed to both, so "the vendor published this" and "we picked
this" cannot quietly swap places between one review and the next.

What the generator does **not** do is judge the catalog. A model whose provider
is absent, a thinking ladder with no supported rung, price tiers that make tier
selection ambiguous — those are invariants of any catalog document, wherever it
came from, and they belong to `catalog::validate` in the kernel, which judges a
served overlay and a user's own configuration by the same rules.

## What the baseline can and cannot say

It can add, describe, re-price, and disable providers and models. It cannot
change route semantics, disclosure classes, redaction policy, or tool
authorization: those live in code and in policy bundles with their own review
path.

One thing it deliberately expresses on a single row is worth knowing. A vendor
reachable both by a key and by a subscription is **one** provider entry, and
which of its two ways in a call takes is decided by the credential in use
rather than by a second entry (decision
0029 section
2). Where the subscription endpoint speaks a different family or sits at a
different address, the row says so in `oauth_wire_api` and `oauth_endpoint`.
Both are optional and independent, both are read only for a credential whose
method is `OAUTH`, and a row that carries either must accept `OAUTH` — an
address a subscription would be spent at, on a provider that accepts no
subscription, is a fact nothing will ever read.

One thing it cannot express is worth knowing before reading the roster.
Candidate order for a role is catalog key order, which is total and identical
on every device, and there is no per-role priority field. Where the intended
first candidate for a role is not the first by key, the source says so in that
model's `provenance.note`, and expressing the intent needs the assistant's own
per-role preference in `route::ModelPolicy` rather than a catalog edit.

## Regenerating

Editing anything under `source/` and not regenerating leaves a tree that
compiles and ships the previous catalog. Run `--write`, then commit
`baseline.json` in the same change as the source that produced it.

Status labels used here are explained under Documentation conventions in
[AGENTS.md](../../../../../../../AGENTS.md#documentation-conventions).
