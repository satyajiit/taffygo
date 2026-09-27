// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A provider that serves its own model list, read on the device
//! (decision 0098).
//!
//! One provider in the launch set has always been described as serving its
//! model list rather than shipping one, and it has always shipped with none:
//! the row is present, a key can be saved against it, and there is nothing to
//! route to. This protocol is the missing half. It has three moments —
//! planning, one fetch in flight, delivery — and it took that shape from the
//! served catalog's refresh, which had the same three for the same reasons.
//! That module is gone with the host it fetched from (decision 0200), so this
//! is the only place the shape is written down now rather than one of a pair.
//!
//! Three rules from the decision are load-bearing and are each enforced in one
//! place here:
//!
//! - **No credential, no fetch.** An aggregator's list is per-account, so
//!   there is nothing to ask for until the person has connected. Candidates are
//!   assembled by the composition surface from the provider plane's own
//!   credentials, and a provider with none never becomes one.
//! - **One decoder.** What comes back is shaped into the document the published
//!   catalog uses and handed to the *same* fail-closed decoder. A second
//!   decoder would be a second place a remote party decides what this product
//!   believes about a model's window and its price, and the catalog decoder
//!   exists because that is the thing worth being careful about.
//! - **Keep what you had.** Every refusal leaves the models a person could
//!   already reach exactly where they were. A listing that cannot be fetched,
//!   cannot be read, or turns out to describe nothing changes nothing.
//!
//! The last one has a sharp edge the decision calls out and this file answers
//! at [`ProviderListingVerdict::RejectedEmpty`]: until the browser can serve
//! the effect, a build that emits it must still *answer* it. An unserved effect
//! arrives as `Unavailable` and is a transport failure; a success carrying no
//! readable row is refused rather than installed. Neither can be mistaken for
//! "this provider has no models", because neither replaces anything.

use std::collections::BTreeMap;

use model_router::catalog::sanitize::sanitize_overlay;
use model_router::catalog::types::{CatalogDocument, Model};
use model_router::catalog::{parse_document, validate, MergedCatalog};
use model_router::ProviderId;

use crate::composition::user_catalog::baseline_currency;
use crate::wire;

mod dialect;
mod shape;

/// How long a fetched listing stays fresh before a refresh is due.
///
/// Decision 0098 section 5 gives the terms: a day. This used to read the
/// served catalog's `CATALOG_REFRESH_DUE_MS`, on the argument that two numbers
/// meaning "the same terms" is how they stop being the same. The argument was
/// right and its subject is gone — decision 0200 leaves no served catalog and
/// so no cadence to agree with — and what is left is a listing a person's own
/// provider answers, whose own terms these are.
pub const LISTING_REFRESH_DUE_MS: u64 = 24 * 60 * 60 * 1000;

/// Hard response cap the browser enforces from the transfer length.
///
/// The contract's own bound, read across rather than retyped, so the size the
/// browser refuses to buffer and the size the core asked for are one number.
#[allow(clippy::cast_possible_truncation)]
pub const MAX_LISTING_RESPONSE_BYTES: u32 = wire::MAX_PROVIDER_LISTING_BYTES as u32;

/// Most models one provider's listing may contribute.
///
/// An aggregator lists hundreds where the published catalog carries dozens, so
/// the count is capped after decoding as well as by transfer length before it
/// (decision 0098 section 4). What settles the number is not the decoder — its
/// document bound is thousands — but the surface at the other end: the Core
/// API status carries one flat model list bounded by
/// `MAX_PROVIDER_MODEL_ENTRIES`, and every provider shares it. The projection
/// used to cut that list in identity order, so a listing allowed to fill the
/// budget pushed every provider sorting after it out of the person's picker,
/// silently and on every device; it now deals the budget out a round at a time
/// and can no longer empty anybody. What is left of the original reason still
/// holds: a listing at the surface's own bound would pull every other
/// provider's share down to the floor. The bound is set far inside the budget
/// so a listing costs the rest of the roster nothing in practice, and the
/// assertion below is what keeps the two numbers from drifting into agreement
/// by accident.
pub const MAX_LISTED_MODELS: usize = 64;

const _: () = assert!(
    MAX_LISTED_MODELS.saturating_mul(4) <= core_api_types::MAX_PROVIDER_MODEL_ENTRIES,
    "a listing must not be able to crowd the published catalog out of the picker"
);

/// One provider whose listing may be fetched right now.
///
/// Assembled by the composition surface, which is the only place that can see
/// both halves of the precondition: the merged catalog says the models are
/// served, and the provider plane says a credential is on file to serve them
/// with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListingCandidate {
    /// The provider whose list this is.
    pub provider_id: String,
    /// The origin the browser composes the listing path from. The core names an
    /// origin and never a path, here as everywhere else.
    pub endpoint: String,
    /// The family whose listing path the browser composes.
    pub wire_api: wire::ProviderWireApi,
    /// The opaque handle for the credential the fetch is made with.
    pub credential_handle: String,
}

/// What one delivered listing did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProviderListingVerdict {
    /// Models were installed for this provider; the caller must rebuild the
    /// merge and republish. `offered` is what the listing carried and `kept` is
    /// what survived decoding and the bound, so a truncated list is never
    /// presented as the whole list.
    Accepted {
        /// The provider whose models were replaced.
        provider_id: String,
        /// How many rows the listing carried.
        offered: usize,
        /// How many of them this build kept.
        kept: usize,
    },
    /// The provider confirmed the list this device holds is still current.
    NotModified,
    /// The transport failed, or this build cannot serve the fetch at all.
    /// Nothing changed and the next poke may retry.
    TransportFailed,
    /// The body is not a listing this build can read.
    RejectedMalformed,
    /// The rows decoded but break an invariant the catalog holds.
    RejectedViolations,
    /// The answer described no model this build can route to.
    ///
    /// Kept apart from acceptance on purpose. An empty answer is not an empty
    /// catalog: it is a provider this build could not read, and replacing a
    /// working list with nothing on the strength of it is exactly the failure
    /// decision 0098's last consequence names.
    RejectedEmpty,
    /// No fetch was in flight for this provider, so this answers nothing that
    /// was asked.
    UnexpectedResult,
}

/// What one provider's listing left behind.
#[derive(Clone, Debug, PartialEq, Eq)]
struct ListingRecord {
    models: Vec<Model>,
    offered: usize,
}

/// Profile-scoped state of every provider that serves its own model list.
///
/// Documents and instants only: no bytes it did not decode, no socket, no clock
/// of its own.
#[derive(Clone, Debug, Default)]
pub struct ProviderListingProtocol {
    listings: BTreeMap<String, ListingRecord>,
    in_flight: Option<String>,
    last_confirmed_utc_ms: BTreeMap<String, u64>,
    attempts: u64,
}

impl ProviderListingProtocol {
    /// Plans one listing fetch if any candidate is due, marking it in flight.
    ///
    /// At most one exists at a time, the same rule the catalog refresh keeps
    /// and for the same reason: a slow network must not be able to stack
    /// requests. Candidates are tried in the order the caller supplies, which
    /// is the merged catalog's own identity order, so which provider is asked
    /// first is a property of the catalog rather than of the moment.
    pub fn begin_refresh(
        &mut self,
        candidates: &[ListingCandidate],
        generation: u64,
        now_utc_ms: u64,
    ) -> Option<wire::EffectEnvelope> {
        if self.in_flight.is_some() {
            return None;
        }
        let candidate = candidates.iter().find(|candidate| {
            self.last_confirmed_utc_ms
                .get(&candidate.provider_id)
                .is_none_or(|confirmed| {
                    now_utc_ms.saturating_sub(*confirmed) >= LISTING_REFRESH_DUE_MS
                })
        })?;
        self.attempts = self.attempts.saturating_add(1);
        let attempt = self.attempts;
        self.in_flight = Some(candidate.provider_id.clone());
        let provider_id = candidate.provider_id.as_str();
        Some(wire::EffectEnvelope {
            operation: wire::OperationEnvelope {
                operation_id: format!("provider-listing-{provider_id}-{attempt}"),
                service_generation: generation,
                task_revision: 0,
                deadline_monotonic_ms: 0,
                idempotency_key: format!("provider-listing-key-{provider_id}-{attempt}"),
            },
            // Names the provider the fetch is about and this process's attempt
            // ordinal, never a position in the candidate list: an identity
            // taken from a position collides the moment the list shortens, and
            // the journal refuses the second claim silently.
            effect_id: format!("provider-listing-{provider_id}-{attempt}"),
            kind: wire::EffectKind::FetchProviderListing,
            // Read-only and safe to repeat: a listing fetched twice describes
            // the same account's models twice.
            retry_class: wire::RetryClass::Idempotent,
            storage_commit: None,
            page_observation: None,
            model_request: None,
            network_request: None,
            browser_action: None,
            tool_job: None,
            secure_store: None,
            auth_surface: None,
            permission_request: None,
            asset_delivery: None,
            catalog_fetch: None,
            provider_listing_fetch: Some(wire::ProviderListingFetchEffect {
                provider_id: candidate.provider_id.clone(),
                endpoint: candidate.endpoint.clone(),
                wire_api: candidate.wire_api,
                credential_handle: Some(candidate.credential_handle.clone()),
                max_response_bytes: MAX_LISTING_RESPONSE_BYTES,
            }),
            composer_completion: None,
            custom_endpoint_probe: None,
        })
    }

    /// Judges one delivered listing on the ordered core sequence.
    ///
    /// `merged` is asked what the provider row is now; `baseline` is what
    /// decision 0080's endpoint guard measures against. They are separate
    /// arguments because they answer separate questions, and folding them
    /// would make the guard compare a served claim against another served
    /// claim.
    pub fn deliver_fetch_result(
        &mut self,
        baseline: &CatalogDocument,
        merged: &MergedCatalog,
        result: &wire::ProviderListingFetchResult,
        now_utc_ms: u64,
    ) -> ProviderListingVerdict {
        if self.in_flight.as_deref() != Some(result.provider_id.as_str()) {
            return ProviderListingVerdict::UnexpectedResult;
        }
        self.in_flight = None;
        match result.disposition {
            wire::CatalogFetchDisposition::Success => {
                let verdict = self.adopt(baseline, merged, result);
                if matches!(verdict, ProviderListingVerdict::Accepted { .. }) {
                    self.last_confirmed_utc_ms
                        .insert(result.provider_id.clone(), now_utc_ms);
                }
                verdict
            }
            wire::CatalogFetchDisposition::NotModified => {
                self.last_confirmed_utc_ms
                    .insert(result.provider_id.clone(), now_utc_ms);
                ProviderListingVerdict::NotModified
            }
            // Unavailable is what a build with no fetcher answers, and it is
            // read as what it is: the endpoint was not heard from. Nothing is
            // confirmed, so the next poke asks again.
            wire::CatalogFetchDisposition::Unavailable
            | wire::CatalogFetchDisposition::Oversized
            | wire::CatalogFetchDisposition::MalformedTransport => {
                ProviderListingVerdict::TransportFailed
            }
        }
    }

    /// Shapes, decodes, validates, guards and bounds one delivered listing.
    fn adopt(
        &mut self,
        baseline: &CatalogDocument,
        merged: &MergedCatalog,
        result: &wire::ProviderListingFetchResult,
    ) -> ProviderListingVerdict {
        let Ok(provider_id) = ProviderId::new(result.provider_id.as_str()) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        // The fetch was planned from this merge one moment ago, so a provider
        // the merge no longer carries means the catalog moved underneath the
        // answer. It reads as unreadable rather than as its own verdict
        // because the outcome is the one that matters and is the same: the
        // plane keeps what it had, and the next poke plans against the catalog
        // as it now is.
        let Some(provider) = merged.provider(&provider_id) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        let Some(currency) = baseline_currency(baseline) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        let Ok(text) = core::str::from_utf8(&result.body) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        let Some(shaped) = shape::shape_listing(provider_id.as_str(), currency, text) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        let Ok(parsed) = parse_document(&shaped.document) else {
            return ProviderListingVerdict::RejectedMalformed;
        };
        // The provider row the models are judged against is the merge's own,
        // not one rebuilt from the listing: a provider describing its own row
        // is the repoint decision 0080 forbids, wearing a bigger field.
        let judged = CatalogDocument {
            header: parsed.document.header,
            providers: vec![provider.clone()],
            models: parsed.document.models,
        };
        // Entry defects already dropped rows inside the decoder; a cross-entry
        // violation refuses the answer whole, because the surviving half of a
        // broken invariant is not a shorter list — it is a different one.
        if !validate(&judged).is_empty() {
            return ProviderListingVerdict::RejectedViolations;
        }
        let guarded = sanitize_overlay(baseline, &judged);
        if !guarded.refusals.is_empty() {
            // Refused, not sanitized (decision 0098 section 3). The guard
            // strips an endpoint and carries the row on, which is right for a
            // published catalog and wrong here: a provider that answers a
            // request for its models with an address is doing something this
            // path does not do, and the rest of what it said is not more
            // trustworthy for having been trimmed.
            return ProviderListingVerdict::RejectedViolations;
        }
        self.install(&result.provider_id, guarded.document.models, shaped.offered)
    }

    /// Installs what survived, keeping the bound's own written rule.
    fn install(
        &mut self,
        provider_id: &str,
        mut models: Vec<Model>,
        offered: usize,
    ) -> ProviderListingVerdict {
        if models.is_empty() {
            return ProviderListingVerdict::RejectedEmpty;
        }
        // Which models survive the bound is decided here and stated: ascending
        // model identity, which is the merge's own total order. Arrival order
        // is the provider's choice and can differ between two fetches of one
        // unchanged list, so a bound applied to it would keep a different set
        // each time and on each device. Identity order makes the kept set a
        // property of the list rather than of the moment it was served.
        models.sort_by(|left, right| left.model_id.cmp(&right.model_id));
        models.truncate(MAX_LISTED_MODELS);
        let kept = models.len();
        self.listings
            .insert(provider_id.to_owned(), ListingRecord { models, offered });
        ProviderListingVerdict::Accepted {
            provider_id: provider_id.to_owned(),
            offered,
            kept,
        }
    }

    /// Discards one provider's listing, for a credential that went away.
    ///
    /// A listing is per-account and was fetched with a credential; when the
    /// credential goes the list stops being about anybody, and leaving it
    /// behind would offer models the next request has nothing to reach them
    /// with. Returns whether anything was held.
    pub fn forget(&mut self, provider_id: &str) -> bool {
        self.last_confirmed_utc_ms.remove(provider_id);
        self.listings.remove(provider_id).is_some()
    }

    /// Every fetched model, in provider then model identity order, for the
    /// person's own layer of the merged catalog.
    pub fn models(&self) -> impl Iterator<Item = &Model> {
        self.listings.values().flat_map(|record| &record.models)
    }

    /// How many models one provider offered and how many this build kept.
    ///
    /// Reachable rather than only recorded: decision 0098 section 4 says the
    /// surface has to be able to say how many were kept, and a count nothing
    /// can read is a count nobody made.
    pub fn kept_for(&self, provider_id: &str) -> Option<(usize, usize)> {
        self.listings
            .get(provider_id)
            .map(|record| (record.offered, record.models.len()))
    }

    /// Every provider this protocol holds a list for.
    pub fn listed_providers(&self) -> impl Iterator<Item = &str> {
        self.listings.keys().map(String::as_str)
    }

    /// Whether a listing fetch is currently in flight.
    pub fn in_flight(&self) -> bool {
        self.in_flight.is_some()
    }
}

#[cfg(test)]
mod dialect_tests;
#[cfg(test)]
mod tests;
