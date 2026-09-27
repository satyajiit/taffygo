// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One content-free proposal around a canonical typed intent.

use bip_types::identity::{ContentDigest, SemanticNodeId, TabId};

use super::ActionIntent;
use crate::authority::ActionClass;
use crate::budget::BudgetDraw;
use crate::ids::{IdempotencyKey, PlanStepId};
use crate::tool::{IdempotencyClass, ToolRuntime};

/// What the task engine wants to happen.
///
/// The intent is the one authoritative description of the operation. Its
/// action class, target, destination and recovery class are derived rather
/// than repeated as independently mutable fields. Model-authored text is
/// represented only by an opaque, digest-bound reference inside the intent.
#[derive(Clone, Debug, PartialEq)]
pub struct ActionProposal {
    intent: ActionIntent,
    /// The plan step it belongs to, when it belongs to one.
    pub plan_step_id: Option<PlanStepId>,
    /// The key that makes a repeat recognizable.
    pub idempotency_key: IdempotencyKey,
    /// Whether the step may not succeed without this action.
    pub required_for_step: bool,
    /// How much of which budget the action draws, when it draws on one.
    pub budget_draw: Option<BudgetDraw>,
    /// Digest of the canonical intent and any transient operands it binds.
    pub proposal_digest: ContentDigest,
}

impl ActionProposal {
    /// Builds a proposal around its one authoritative operation.
    pub const fn new(
        intent: ActionIntent,
        plan_step_id: Option<PlanStepId>,
        idempotency_key: IdempotencyKey,
        required_for_step: bool,
        budget_draw: Option<BudgetDraw>,
        proposal_digest: ContentDigest,
    ) -> Self {
        Self {
            intent,
            plan_step_id,
            idempotency_key,
            required_for_step,
            budget_draw,
            proposal_digest,
        }
    }

    pub const fn intent(&self) -> &ActionIntent {
        &self.intent
    }

    pub fn tool_name(&self) -> &str {
        self.intent.tool_name()
    }

    pub const fn action_class(&self) -> ActionClass {
        self.intent.action_class()
    }

    pub const fn tab_id(&self) -> &TabId {
        self.intent.tab_id()
    }

    pub const fn node_id(&self) -> Option<&SemanticNodeId> {
        self.intent.node_id()
    }

    pub fn destination_address(&self) -> Option<&str> {
        self.intent.destination_address()
    }

    pub const fn idempotency(&self) -> IdempotencyClass {
        self.intent.idempotency()
    }

    pub const fn tool_runtime(&self) -> Option<ToolRuntime> {
        self.intent.tool_runtime()
    }
}
