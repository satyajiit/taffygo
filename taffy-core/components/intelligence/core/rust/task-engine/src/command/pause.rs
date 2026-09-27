// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why a task is being paused.

/// Why the task is being paused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PauseCause {
    /// The user asked for it.
    User,
    /// The platform restricted background work.
    BackgroundRestricted,
    /// The person's own allowance at the provider was reached — credits,
    /// billing state, or their plan's rate limit. The task can resume later.
    ProviderLimit,
    /// The provider could not serve the request right now (decision 0219).
    ///
    /// Its capacity, not the person's allowance: an HTTP 5xx, which says
    /// nothing about their account. Separate from [`Self::ProviderLimit`]
    /// because telling somebody their limit was reached when it was not sends
    /// them to check a plan that is fine, and to switch a model that was
    /// never the problem.
    ProviderBusy,
    /// The device went offline; the task can resume when it is back.
    Offline,
    /// A paid model call left and nothing came back, so what became of it is
    /// not known (decision 0217).
    ///
    /// Distinct from [`Self::Offline`], which says the device had no network.
    /// This one claims nothing about why: the socket may have died in a
    /// handover with the request already written, and the provider may have
    /// answered into it and billed for the answer. Pausing rather than failing
    /// is what keeps the task's work, and it mints nothing — the next paid
    /// identity is minted only if a person resumes.
    NoAnswer,
}

impl PauseCause {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::User => "user",
            Self::BackgroundRestricted => "background_restricted",
            Self::ProviderLimit => "provider_limit",
            Self::ProviderBusy => "provider_busy",
            Self::Offline => "offline",
            Self::NoAnswer => "no_answer",
        }
    }
}
