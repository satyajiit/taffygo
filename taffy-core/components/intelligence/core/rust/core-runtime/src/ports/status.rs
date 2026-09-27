// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic decoration of the status projection (decision 0073).
//!
//! A contributor fills fields the runtime does not own and must not remove or
//! contradict a runtime fact. Contributors on disjoint fields commute, the
//! decorated payload still passes the encoder's bounds, and a contributor
//! cannot flip availability or drop a task — each of those is a test. The
//! facts a contributor may project from arrive borrowed at call time, so
//! transient state keeps exactly one owner and dies with its generation.

use std::collections::BTreeMap;

use crate::account::AccountAuthMethod;
use crate::entitlement_refresh::EntitlementDisplay;
use crate::probe::ProbeDisplay;
use crate::provider::ProviderStatusProjection;

/// Borrowed, typed facts a contributor may project from. A future contributor
/// that needs a fact the runtime owns adds a field here — a typed edit at the
/// one seam — rather than capturing state of its own.
#[derive(Debug)]
pub struct StatusContributionFacts<'a> {
    /// The validated compiled built-in rows for this exact configuration and
    /// installed-part state.
    pub builtin_skills: &'a [core_api_types::BuiltinSkillView],
    /// The account methods this installation can actually start.
    pub available_account_methods: &'a [AccountAuthMethod],
    /// Classified ask prompts by task id, shown while a task waits on the
    /// person and dropped on answer.
    pub ask_prompts: &'a BTreeMap<String, String>,
    /// The provider plane's eager bounded answer at its latest accepted write.
    /// Publications borrow it; they never rebuild the roster or re-sort the
    /// merged model catalog.
    pub provider_status: &'a ProviderStatusProjection,
    /// The managed entitlement's surface facts, present only when a mint has
    /// produced a summary this process lifetime that states an entitlement.
    pub entitlement: Option<&'a EntitlementDisplay>,
    /// The latest probe verdict per provider (decision 0083), in stable
    /// provider order; a provider never probed has no row.
    pub provider_probes: &'a [ProbeDisplay],
}

/// The projection-decoration port.
pub trait StatusContributorPort {
    /// Deterministically decorates the projection. May fill fields the
    /// runtime does not own; must not remove or contradict a runtime fact.
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    );
}

impl<T> StatusContributorPort for Box<T>
where
    T: StatusContributorPort + ?Sized,
{
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        self.as_ref().contribute(facts, status);
    }
}
