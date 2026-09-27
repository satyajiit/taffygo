// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact Tool Runtime request and terminal-completion boundary.

use core::fmt;

use core_service_types as wire;

mod validation;

/// Why a generated tool job or completion was refused by the core.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ToolContractError {
    InvalidBody,
    InvalidIdentifier,
    InvalidBudget,
    InputTooLarge,
    OutputTooLarge,
    TooManyProgressEvents,
    TooManyOutputChunks,
    InvalidProgress,
    InvalidSequence,
    WrongJob,
    WrongOperation,
    /// No compiled-in registry row carries the named entrypoint.
    ///
    /// There is no second place to look and no way to add one after the build,
    /// so this refusal is final for the life of the binary rather than a
    /// lookup that might succeed later (decision 0064).
    UnknownEntrypoint,
    /// The named entrypoint exists and a native path owns the capability.
    ///
    /// The payload is that native owner's identity, so a refusal can say where
    /// the answer actually comes from instead of only that a worker will not
    /// be started.
    EntrypointHasNativeAlternative(&'static str),
}

/// One validated request for a runtime-specific sandbox.
#[derive(Clone, PartialEq, Eq)]
pub struct ToolEffect {
    wire: wire::ToolJobEffect,
}

impl ToolEffect {
    /// Accepts only the exact generated body and runtime-operation pairing.
    pub fn new(job: wire::ToolJobEffect) -> Result<Self, ToolContractError> {
        validation::validate_job(&job)?;
        Ok(Self { wire: job })
    }

    /// Opaque job identity used for completion correlation.
    pub fn job_id(&self) -> &str {
        &self.wire.job_id
    }

    /// Isolated runtime family selected by this job.
    pub const fn runtime(&self) -> wire::ToolRuntimeKind {
        self.wire.runtime
    }

    /// Closed reviewed operation selected by this job.
    pub const fn operation(&self) -> wire::ToolOperation {
        self.wire.operation_kind
    }

    /// Exact generated broker request.
    pub fn to_wire(&self) -> wire::ToolJobEffect {
        self.wire.clone()
    }

    /// Validates the exactly-one terminal result against this request.
    pub fn accept_completion(
        &self,
        result: wire::ToolEffectResult,
    ) -> Result<ToolCompletion, ToolContractError> {
        validation::validate_completion(&self.wire, &result)?;
        Ok(ToolCompletion { wire: result })
    }
}

impl fmt::Debug for ToolEffect {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("ToolEffect")
            .field("job_id", &self.wire.job_id)
            .field("runtime", &self.wire.runtime)
            .field("tool_id", &self.wire.tool_id)
            .field("tool_version", &self.wire.tool_version)
            .field("operation", &self.wire.operation_kind)
            .finish_non_exhaustive()
    }
}

/// One validated terminal tool result with ordered progress and chunks.
#[derive(Clone, PartialEq, Eq)]
pub struct ToolCompletion {
    wire: wire::ToolEffectResult,
}

impl ToolCompletion {
    /// Opaque job identity already checked against the originating effect.
    pub fn job_id(&self) -> &str {
        &self.wire.job_id
    }

    /// Exact generated terminal result.
    pub fn to_wire(&self) -> wire::ToolEffectResult {
        self.wire.clone()
    }
}

impl fmt::Debug for ToolCompletion {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("ToolCompletion")
            .field("job_id", &self.wire.job_id)
            .field("status", &self.wire.status)
            .field("progress_events", &self.wire.progress.len())
            .field("output_chunks", &self.wire.chunks.len())
            .finish_non_exhaustive()
    }
}

#[cfg(test)]
mod tests;
