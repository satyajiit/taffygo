// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed result of decoding one browser-owned task checkpoint.

use task_engine::Effect;

use crate::ports::TaskEngineLoad;

/// The final committed reducer-effect intent batch, if it has no later
/// transaction proving that its terminal completion was observed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RestoredTaskEffectBatch {
    pub parent_operation_id: String,
    pub task_revision: u64,
    pub effects: Vec<Effect>,
}

/// Canonical reducer replay input plus its still-unresolved final effects.
#[derive(Debug)]
pub struct DecodedTaskRestore {
    pub load: TaskEngineLoad,
    pub unresolved_effects: Option<RestoredTaskEffectBatch>,
}
