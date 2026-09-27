// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The scheduler walk: which durable command a task is asked next.
//!
//! One walk, chosen by the consented route: the reviewed no-model sequence or
//! the assistant decision table, never the other's empty answer. [`advance`]
//! owns the ordering rules — refused-call gap first, loop-local settling,
//! then the route-owned scheduler and the replay-stable envelope identity —
//! and the service bridge is transport around it (decision 0072).

pub mod advance;
pub mod agent;
pub mod procedure;
mod research;
pub mod reviewed;

pub use research::bind_research_result;

pub use advance::{
    advance, advance_with_procedure, artifact_failure_command, settle_loop_calls, PlannedCommand,
    WalkError, MODEL_CALL_DEADLINE_MS, WORKFLOW_COMMAND_DEADLINE_MS,
};
pub use agent::{
    next_agent_command_envelope, next_agent_command_envelope_after_gap,
    next_durable_command_envelope, next_durable_command_envelope_after_gap,
    next_durable_command_envelope_with_procedure, next_pre_model_observation_envelope,
    workflow_for_provider_route, AgentWorkflowError, DurableWorkflowError,
    PreModelObservationEnvelope, TaskWorkflow,
};
pub use procedure::{next_procedure_command_envelope, ProcedureWorkflowError};
pub use reviewed::{next_reviewed_command_envelope, ReviewedWorkflowError};
