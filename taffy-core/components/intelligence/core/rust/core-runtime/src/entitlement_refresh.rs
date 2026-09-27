// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed entitlement's lifecycle on the device (decision 0082).
//!
//! One protocol owns two moments: planning, when a browser poke asks whether
//! the account's entitlement summary should be fetched again; and delivery,
//! when the one fetch in flight answers. The fetch itself is a mint at the
//! product's own worker, performed by the browser through the account broker
//! — the token the mint produces stays in the browser process, and what
//! crosses the seam back is [`wire::EntitlementSummaryResult`], a record with
//! no field a bearer credential could ride.
//!
//! Every path keeps the last known state rather than blanking it: a transport
//! failure leaves the previous summary standing, because "the worker was
//! unreachable for a moment" and "the person has no entitlement" are different
//! facts and only a definitive answer may install the second. The one
//! installer of *unavailable* is a summary whose `definitive_absent` is set —
//! the worker's own signed-in answer that no entitlement exists.
//!
//! Four reasons may plan a fetch and they are gated differently, because they
//! say different things about how stale the held summary is. `BOOTSTRAP` and
//! `SIGN_IN` plan immediately — there is nothing held, or what is held is
//! another moment's. `CADENCE` refreshes a summary that has merely aged, on
//! [`ENTITLEMENT_REFRESH_DUE_MS`]. `QUOTA_REFUSED` follows a managed dispatch
//! the worker refused for quota, where a fresh summary may already say
//! otherwise (a top-up, a window turn); it is rate-gated by
//! [`ENTITLEMENT_RETRY_FLOOR_MS`] against a refusal loop asking in a tight
//! circle.

use crate::wire;

/// How long an installed summary stays fresh before a cadence poke replans.
pub const ENTITLEMENT_REFRESH_DUE_MS: u64 = 6 * 60 * 60 * 1000;

/// The shortest interval between two planned fetches for `QUOTA_REFUSED`.
pub const ENTITLEMENT_RETRY_FLOOR_MS: u64 = 60 * 1000;

/// Hard response cap the browser enforces from the transfer length.
pub const MAX_ENTITLEMENT_RESPONSE_BYTES: u32 = 65_536;

/// What one delivered entitlement fetch did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EntitlementFetchVerdict {
    /// A summary was installed. The caller rebuilds the managed entitlement
    /// from it and republishes status; `definitive_absent` says whether the
    /// worker's answer was "no entitlement exists", which installs
    /// unavailable rather than nothing.
    Installed {
        /// The worker's definitive "no entitlement" answer.
        definitive_absent: bool,
    },
    /// The transport failed; the last summary stands and a later poke may
    /// retry.
    TransportFailed,
    /// No fetch was in flight, so this result answers nothing that was asked.
    UnexpectedResult,
}

/// The five facts a surface draws, projected off the held summary.
///
/// A separate value rather than a borrow of the wire record, so the status
/// seam carries exactly what the Core API's `EntitlementView` states and a
/// field added to the summary does not become surface-visible by accident.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EntitlementDisplay {
    /// The plan's stable identifier.
    pub plan_id: String,
    /// Credits granted for the current period.
    pub credits_granted: u64,
    /// Credits remaining, as of the mint that produced the summary.
    pub credits_remaining: u64,
    /// When the credit grant next turns, in epoch seconds; zero when the
    /// worker stated none.
    pub next_renewal_epoch_seconds: u64,
    /// When the plan itself lapses, in epoch seconds; zero when open-ended.
    pub valid_until_epoch_seconds: u64,
}

/// Profile-scoped state of the managed entitlement summary.
///
/// Holds the last installed summary and instants only: no token, no socket,
/// no clock of its own. The token the summary arrived beside cannot reach
/// this type — the wire record it is built from has no field for one.
#[derive(Clone, Debug, Default)]
pub struct EntitlementRefreshProtocol {
    summary: Option<wire::EntitlementSummaryResult>,
    last_installed_utc_ms: Option<u64>,
    last_planned_utc_ms: Option<u64>,
    in_flight: bool,
    attempts: u64,
}

impl EntitlementRefreshProtocol {
    /// Plans one fetch if `reason` warrants it, marking it in flight.
    ///
    /// At most one fetch exists at a time; a poke while one is in flight is
    /// answered with nothing rather than a second effect, so a slow network
    /// cannot stack mints.
    pub fn begin_refresh(
        &mut self,
        generation: u64,
        reason: wire::EntitlementFetchReason,
        now_utc_ms: u64,
    ) -> Option<wire::EffectEnvelope> {
        if self.in_flight {
            return None;
        }
        match reason {
            wire::EntitlementFetchReason::Bootstrap | wire::EntitlementFetchReason::SignIn => {}
            wire::EntitlementFetchReason::Cadence => {
                if let Some(installed) = self.last_installed_utc_ms {
                    if now_utc_ms.saturating_sub(installed) < ENTITLEMENT_REFRESH_DUE_MS {
                        return None;
                    }
                }
            }
            wire::EntitlementFetchReason::QuotaRefused => {
                if let Some(planned) = self.last_planned_utc_ms {
                    if now_utc_ms.saturating_sub(planned) < ENTITLEMENT_RETRY_FLOOR_MS {
                        return None;
                    }
                }
            }
        }
        self.in_flight = true;
        self.last_planned_utc_ms = Some(now_utc_ms);
        self.attempts = self.attempts.saturating_add(1);
        let attempt = self.attempts;
        Some(wire::EffectEnvelope {
            operation: wire::OperationEnvelope {
                operation_id: format!("entitlement-refresh-{generation}-{attempt}"),
                service_generation: generation,
                task_revision: 0,
                deadline_monotonic_ms: 0,
                idempotency_key: format!("entitlement-refresh-key-{generation}-{attempt}"),
            },
            effect_id: format!("entitlement-fetch-{generation}-{attempt}"),
            kind: wire::EffectKind::NetworkRequest,
            // Safe to repeat: a mint that runs twice produces two summaries
            // saying the same thing, and the worker's own rails bound how
            // often it will say it.
            retry_class: wire::RetryClass::Idempotent,
            storage_commit: None,
            page_observation: None,
            model_request: None,
            network_request: Some(wire::NetworkRequestEffect {
                operation_kind: wire::AccountNetworkOperation::FetchEntitlement,
                exchange_authorization_code: None,
                exchange_native_credential: None,
                request_email_link: None,
                refresh_session: None,
                revoke_session: None,
                max_response_bytes: MAX_ENTITLEMENT_RESPONSE_BYTES,
                fetch_entitlement: Some(wire::FetchEntitlementRequest { reason }),
            }),
            browser_action: None,
            tool_job: None,
            secure_store: None,
            auth_surface: None,
            permission_request: None,
            asset_delivery: None,
            catalog_fetch: None,
            provider_listing_fetch: None,
            composer_completion: None,
            custom_endpoint_probe: None,
        })
    }

    /// Judges one delivered fetch result on the ordered core sequence.
    ///
    /// `summary` is the browser's answer: the worker's summary when the mint
    /// reached a definitive verdict, or nothing when the transport failed.
    pub fn deliver_fetch_result(
        &mut self,
        summary: Option<&wire::EntitlementSummaryResult>,
        now_utc_ms: u64,
    ) -> EntitlementFetchVerdict {
        if !self.in_flight {
            return EntitlementFetchVerdict::UnexpectedResult;
        }
        self.in_flight = false;
        match summary {
            Some(summary) => {
                let definitive_absent = summary.definitive_absent;
                self.summary = Some(summary.clone());
                self.last_installed_utc_ms = Some(now_utc_ms);
                EntitlementFetchVerdict::Installed { definitive_absent }
            }
            None => EntitlementFetchVerdict::TransportFailed,
        }
    }

    /// Forgets everything held, for the moment the signed-in account goes.
    ///
    /// Also drops the in-flight marker, so a mint still running for the
    /// departed account delivers into `UnexpectedResult` and installs
    /// nothing.
    pub fn clear(&mut self) {
        self.summary = None;
        self.last_installed_utc_ms = None;
        self.last_planned_utc_ms = None;
        self.in_flight = false;
    }

    /// The last installed summary, for the caller that rebuilds the managed
    /// entitlement.
    pub fn summary(&self) -> Option<&wire::EntitlementSummaryResult> {
        self.summary.as_ref()
    }

    /// The surface projection, present only when a mint has produced a
    /// summary this process lifetime and that summary states an entitlement.
    pub fn display(&self) -> Option<EntitlementDisplay> {
        let summary = self.summary.as_ref()?;
        if summary.definitive_absent {
            return None;
        }
        Some(EntitlementDisplay {
            plan_id: summary.plan_id.clone(),
            credits_granted: summary.credits_granted,
            credits_remaining: summary.credits_remaining,
            next_renewal_epoch_seconds: summary.next_renewal_epoch_seconds,
            valid_until_epoch_seconds: summary.valid_until_epoch_seconds,
        })
    }
}

#[cfg(test)]
mod tests;
