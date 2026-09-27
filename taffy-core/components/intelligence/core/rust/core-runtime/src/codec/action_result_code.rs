// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The BIP action result code on the core-service wire.
//!
//! `TaskActionResultCode` mirrors `bip_types::ActionResultCode` member for
//! member, in the order the BIP protocol declares normative, so that a browser
//! refusal and a Rust policy refusal end an action under one closed
//! vocabulary. The two enumerations are kept in lockstep by the test below
//! rather than by review: a member added to one and not the other fails here.

use bip_types::ActionResultCode;
use core_service_types as wire;

macro_rules! closed_pair {
    ($encode:ident, $decode:ident, $source:ty, $target:ty, $($variant:ident),+ $(,)?) => {
        /// Projects the BIP result code onto the core-service wire.
        pub const fn $encode(value: $source) -> $target {
            match value { $(<$source>::$variant => <$target>::$variant),+ }
        }
        /// Reads a core-service wire code back as the BIP result code.
        pub const fn $decode(value: $target) -> $source {
            match value { $(<$target>::$variant => <$source>::$variant),+ }
        }
    };
}

closed_pair!(
    action_result_code_to_wire,
    action_result_code_from_wire,
    ActionResultCode,
    wire::TaskActionResultCode,
    Verified,
    DeniedByPolicy,
    ApprovalRequired,
    ApprovalDenied,
    ActorLeaseMissing,
    CapabilityExpired,
    TabGone,
    FrameGone,
    DocumentInactive,
    StalePageEpoch,
    StaleGraph,
    NodeGone,
    OriginChanged,
    RoleOrActionChanged,
    NotVisible,
    Occluded,
    NotEnabled,
    NotEditable,
    SensitiveField,
    DestinationChanged,
    Unsupported,
    BudgetExceeded,
    DispatchFailed,
    NavigationStarted,
    PostconditionTimeout,
    PostconditionFailed,
    CancelledByUser,
    CancelledByNavigation,
    RendererCrashed,
    OutcomeUnknown,
    InternalError,
    EgressNotAuthorized,
    DestinationClassRestricted,
    UntrustedContentOrigin,
    PreparedEffectChanged,
    CommitWithoutPrepare,
    GraphMovedDuringPreflight,
    ValueReferenceUnknown
);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_code_crosses_in_the_bip_order_and_comes_back() {
        for (index, code) in ActionResultCode::ALL.iter().enumerate() {
            let crossed = action_result_code_to_wire(*code);
            assert_eq!(usize::try_from(crossed as u32), Ok(index), "{code:?}");
            assert_eq!(action_result_code_from_wire(crossed), *code);
        }
    }
}
