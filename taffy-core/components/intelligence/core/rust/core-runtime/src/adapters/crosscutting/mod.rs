// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The cross-cutting ports' canonical adapters (decision 0073).
//!
//! One observer that counts, two contributors that project. Nothing here
//! decides anything: the observation port cannot alter control flow, and a
//! contributor fills only fields the runtime does not own.

mod observer;
mod status;

pub use observer::ProductionTurnObserver;
pub use status::{
    ProductionAccountMethods, ProductionAskPrompts, ProductionEntitlementView,
    ProductionProviderModels, ProductionProviderProbes, ProductionProviderRoster,
};
