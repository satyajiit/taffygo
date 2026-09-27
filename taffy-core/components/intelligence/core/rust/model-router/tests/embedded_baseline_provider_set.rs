// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which vendors this build says exist.
//!
//! Split from `embedded_baseline.rs` along its one clean seam. That file asks
//! what the shipped rows can *do* — every role routable, every wire family
//! spoken, a plan-backed row priced as a plan. This one asks who is in the list
//! at all, which is a question with different authorities: decision
//! `docs/decisions/0029-launch-provider-set.md` names the eight that may not
//! silently disappear, decision
//! `docs/decisions/0094-the-provider-set-grows-by-catalog-row.md` names what
//! was added by hand from vendor pages, and decision
//! `docs/decisions/0118-the-model-catalog-is-imported-from-a-source-of-record.md`
//! names what was imported. Three registers and one assertion over their union,
//! so a row that appears without a decision behind it fails here.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use model_router::embedded_baseline;

/// The six rows of decision 0029 that still stand. None of them may leave the
/// baseline. Two have been withdrawn and neither is a row this test would
/// miss: `google-antigravity` by decision 0134, and `cloudflare-workers-ai`
/// by decision 0214, which also took the catalog's only EMBEDDING rows with
/// it.
const DECIDED_LAUNCH_SET: [&str; 6] = [
    "anthropic",
    "google-ai-studio",
    "moonshot",
    "openai",
    "openrouter",
    "xai",
];

/// The rows decision 0094 added, each reachable at the origin the row names.
///
/// A vendor is added here in the same change that adds its catalog row, which
/// is what stops the set growing by accident: an overlay or a hand edit that
/// introduces a provider nobody decided fails this test rather than shipping.
///
/// Five of the first eight are reached with no per-provider path at all. Three
/// — `fireworks`, `groq` and `zai` — are the vendors 0094 section 1 named as
/// *not* addable by a row alone, and they are here because the table it said to
/// wait for now exists: `kProviderPathPrefixes` in
/// `//taffy/browser/model/profile_model_broker_routes.cc` compiles `/inference`,
/// `/openai`, and `/api/paas` with a `/v4` version override. That is the order
/// 0094's consequences require, table first and row second, and it is the whole
/// reason the halves of this list are not distinguished below: what a row may
/// say is the same either way.
///
/// `github-copilot` and `kimi-coding` are the first rows added for a
/// **subscription** rather than a key, and they are here because the row was the only one of the four tables
/// a sign-in needs that was missing. `SIGN_IN_VENDORS`, the Android flow map
/// and the browser's vendor table already named `github-copilot` and
/// `kimi-coding`; the catalog did not, so both flows were compiled, tested and
/// unreachable. `kimi-coding` is the same table-first order again — its
/// `/coding` prefix is compiled beside the other three.
///
/// The four listed rows are decision 0114's, and they are the first rows added that
/// carry no models on purpose rather than by omission: each vendor serves its
/// own list and the device reads it with the person's own key, so what this
/// baseline ships for them is the row alone. Three of the four are reached
/// through a prefix compiled in the same change — `/api` for `venice`,
/// `/openai` for `novita`, `/api/gateway` for `kilocode` — and `chutes`
/// needs none, which is the table-first order once more.
const ADDED_BY_CATALOG_ROW: [&str; 17] = [
    "baseten",
    "cerebras",
    "chutes",
    "deepinfra",
    "deepseek",
    "fireworks",
    "github-copilot",
    "groq",
    "kilocode",
    "kimi-coding",
    "minimax",
    "mistral",
    "novita",
    "qwen",
    "together",
    "venice",
    "zai",
];

/// The vendors decision 0118's import added, and the reason they are a
/// separate register rather than more rows above.
///
/// Everything in `ADDED_BY_CATALOG_ROW` was written by hand from a vendor's own
/// page, one field at a time, which is what decision 0094 section 2 asked for
/// and what it costs. These came from a source of record by running
/// `import_pi_catalog.py`, and the difference is worth keeping visible in the
/// one place the provider set is executable: a row here is machine-checked and
/// was not read by a person, and if that trade ever has to be revisited this is
/// the list it applies to.
///
/// Seven of them are reached through a prefix compiled in the same change —
/// `/compatible-mode` for the two Qwen token plans, `/api/coding/paas` with a
/// `/v4` override for `zai-coding-cn`, `/anthropic` for `minimax-cn`, `/zen`
/// and `/zen/go` for the two `OpenCode` rows — which is the table-first order
/// decision 0094's consequences require, once more.
const ADDED_BY_IMPORT: [&str; 15] = [
    "ant-ling",
    "huggingface",
    "minimax-cn",
    "moonshot-cn",
    "nvidia",
    "opencode",
    "opencode-go",
    "qwen-token-plan",
    "qwen-token-plan-cn",
    "vercel-ai-gateway",
    "xiaomi",
    "xiaomi-token-plan-ams",
    "xiaomi-token-plan-cn",
    "xiaomi-token-plan-sgp",
    "zai-coding-cn",
];

#[test]
fn embedded_baseline_carries_the_decided_launch_provider_set() {
    // Decision 0029 names the launch list and 0094 names what has been added
    // to it, and this is the only place either result is executable. The
    // generator refuses to emit a provider whose source rules do not hold;
    // this refuses to ship a build whose baseline lost a decided row or gained
    // an undecided one.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    let ids: Vec<&str> = parsed
        .document
        .providers
        .iter()
        .map(|provider| provider.provider_id.as_str())
        .collect();
    for decided in DECIDED_LAUNCH_SET {
        assert!(
            ids.contains(&decided),
            "the baseline dropped {decided}, which decision 0029 decided"
        );
    }
    let mut expected: Vec<&str> = DECIDED_LAUNCH_SET
        .into_iter()
        .chain(ADDED_BY_CATALOG_ROW)
        .chain(ADDED_BY_IMPORT)
        .collect();
    expected.sort_unstable();
    assert_eq!(
        ids, expected,
        "the baseline's provider list is not the one decisions 0029 and 0094 decided"
    );
}

#[test]
fn every_row_names_an_origin_and_never_a_path() {
    // What this checks did not change when the browser gained a per-vendor
    // prefix table; what it means did. Before, an endpoint carrying a path was
    // a vendor that could not be reached at all, and the assertion was a
    // reachability check. Now three of these vendors are reached *through* a
    // path — `/openai`, `/inference`, `/api/paas/v4` — and the assertion is the
    // authority boundary instead: a path is composed in the browser process
    // from a compiled table, and the sandboxed core may name an origin and
    // nothing else (decision 0049). A served overlay that could put a prefix in
    // an endpoint would be a served document that could move a request, so a
    // row that carries one fails here rather than on a device.
    //
    // Every row rather than the added sets. The rule is true of the decided
    // launch set too, and reading only the two added lists left the oldest
    // rows — the ones a reader is most likely to copy — outside the one
    // assertion that states it. `google-ai-studio` still carries a `/v1beta`
    // this browser silently discards; a row like that under a family whose
    // path prefix mattered would be a request sent somewhere nobody named.
    let (parsed, _) = embedded_baseline().expect("the baseline is valid JSON");
    for provider in &parsed.document.providers {
        let provider_id = provider.provider_id.as_str();
        let endpoint = provider.default_endpoint.as_str();
        let after_scheme = endpoint
            .strip_prefix("https://")
            .expect("every endpoint is https");
        assert!(
            !after_scheme.contains('/'),
            "{provider_id} names {endpoint}, which carries a path the browser would not send"
        );
    }
}
