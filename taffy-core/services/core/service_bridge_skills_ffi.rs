// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! CXX records for one bounded saved-skill mutation.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeSkillOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeSkillClause {
        kind: u8,
        role: u32,
        detail: u32,
    }

    struct BridgeSkillArgument {
        parameter: u32,
        kind: u8,
        value: u64,
        purpose: u32,
        has_public_address: bool,
        public_address: String,
        has_semantic_target: bool,
        semantic_role: u32,
        semantic_phrase: u32,
    }

    struct BridgeSkillStep {
        verb: String,
        arguments: Vec<BridgeSkillArgument>,
        postcondition: u32,
        has_fill: bool,
        fill_purpose: u32,
    }

    struct BridgeSkillCommand {
        operation: BridgeSkillOperation,
        kind: u8,
        skill_id: String,
        expected_version: u32,
        origin: String,
        clauses: Vec<BridgeSkillClause>,
        steps: Vec<BridgeSkillStep>,
        admitted: u32,
        enabled: bool,
        recorded_at_epoch_ms: u64,
    }
    struct BridgeSavedFlowQuery {
        operation: BridgeSkillOperation,
        kind: u8,
        goal: String,
        skill_id: String,
        expected_version: u32,
    }
    struct BridgeSavedFlowReview {
        skill_id: String,
        origin: String,
        provenance: u8,
        status: u8,
        active_version: u32,
        step_count: u32,
        installed_at_epoch_ms: u64,
        updated_at_epoch_ms: u64,
        has_recorded_task: bool,
        recorded_from_task_id: String,
        reviewed_steps: Vec<BridgeSkillStep>,
    }
    struct BridgeSavedFlowQueryResult {
        operation: BridgeSkillOperation,
        status: u8,
        flows: Vec<BridgeSavedFlowReview>,
    }
}
