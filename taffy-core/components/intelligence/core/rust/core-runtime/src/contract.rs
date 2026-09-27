// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed, bounded contracts for work that leaves the ordered core sequence.

mod effect;
mod envelope;
mod payload;
mod tool;

pub use self::effect::{
    BrowserActionEffect, CancellationReason, Completion, EffectRequest, EffectSemantics,
    ObservationEffectError, PageObservationEffect, RuntimeError, StorageCommitEffect,
};
pub use self::envelope::{
    Deadline, EnvelopeError, OperationEnvelope, OperationId, ServiceGeneration,
    MAX_IDEMPOTENCY_KEY_BYTES, MAX_OPERATION_ID_BYTES,
};
pub use self::payload::{BoundedPayload, PayloadError, PayloadLimit, MAX_PAYLOAD_BYTES};
pub use self::tool::{ToolCompletion, ToolContractError, ToolEffect};
