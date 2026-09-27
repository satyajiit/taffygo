// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The composition root for one profile utility service, and nothing else.
//!
//! Every `Production*` adapter is constructed here and only here; the rest of
//! the crate receives what it needs through [`crate::ports`]. Decision 0072
//! names this rule and the check that enforces it.

pub mod profile;
pub mod provider_catalog;
pub mod user_catalog;
