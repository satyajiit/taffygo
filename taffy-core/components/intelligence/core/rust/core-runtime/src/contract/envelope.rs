// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Identity, generation, revision, deadline, and idempotency metadata.

use core::fmt;

use task_engine::ids::IdempotencyKey;

/// Upper bound for an operation identifier at the service boundary.
pub const MAX_OPERATION_ID_BYTES: usize = 128;

/// Upper bound for an idempotency key at the service boundary.
pub const MAX_IDEMPOTENCY_KEY_BYTES: usize = 256;

/// An operation identity correlated with one profile service generation.
///
/// The envelope's generation rejects stale messages. The identity itself may
/// deliberately remain stable across generations when it names unresolved
/// durable logical work (decision
/// `0100-a-durable-effect-identity-outlives-a-generation.md`).
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct OperationId(String);

impl OperationId {
    /// Validates an opaque operation identity.
    pub fn new(value: impl Into<String>) -> Result<Self, EnvelopeError> {
        let value = value.into();
        if value.is_empty() {
            return Err(EnvelopeError::EmptyOperationId);
        }
        if value.len() > MAX_OPERATION_ID_BYTES {
            return Err(EnvelopeError::OperationIdTooLong);
        }
        Ok(Self(value))
    }

    /// The opaque boundary value.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for OperationId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Counts core utility-process incarnations **within one browser session**.
///
/// It is not durable and it is not unique across restarts. The browser holds
/// it in memory from [`INITIAL`](Self::INITIAL), advances it when the utility
/// process disconnects, and writes it to no preference and no table — so every
/// browser launch starts at 1 again and the same number describes a different
/// incarnation each time. Anything that must name *which* incarnation asked
/// has to carry the browser session id beside this, the way a probe identity
/// does (decision `0099-a-probe-identity-names-the-incarnation-that-asked.md`).
/// Decision `0100-a-durable-effect-identity-outlives-a-generation.md` keeps the
/// counter intentionally non-durable and defines what may consume it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ServiceGeneration(u64);

impl ServiceGeneration {
    /// The first process incarnation in a profile session.
    pub const INITIAL: Self = Self(1);

    /// Wraps a generation number the browser supplied. Not a persisted value —
    /// see the type's own documentation before keying anything on it.
    pub const fn new(value: u64) -> Self {
        Self(value)
    }

    /// The wire value.
    pub const fn value(self) -> u64 {
        self.0
    }

    /// The next incarnation, or `None` when the counter is exhausted.
    pub fn next(self) -> Option<Self> {
        self.0.checked_add(1).map(Self)
    }
}

/// A monotonic deadline in milliseconds from the browser-owned clock.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Deadline(u64);

impl Deadline {
    /// Wraps a monotonic deadline.
    pub const fn from_millis(value: u64) -> Self {
        Self(value)
    }

    /// The monotonic wire value.
    pub const fn as_millis(self) -> u64 {
        self.0
    }

    /// Whether work should no longer start or continue.
    pub const fn is_expired_at(self, now_millis: u64) -> bool {
        now_millis >= self.0
    }
}

/// Metadata common to every effect and completion.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OperationEnvelope<T> {
    /// Unique within a generation, or the same durable identity rediscovered
    /// after a utility-process restart.
    pub operation_id: OperationId,
    /// Rejects replies from a dead or replaced service.
    pub service_generation: ServiceGeneration,
    /// Rejects replies written against older task state.
    pub task_revision: u64,
    /// Browser-owned monotonic deadline.
    pub deadline: Deadline,
    /// Makes a repeated request recognizable without repeating its effect.
    pub idempotency_key: IdempotencyKey,
    /// Typed request or terminal completion.
    pub body: T,
}

impl<T> OperationEnvelope<T> {
    /// Builds an envelope after checking boundary-sized identifiers.
    pub fn new(
        operation_id: OperationId,
        service_generation: ServiceGeneration,
        task_revision: u64,
        deadline: Deadline,
        idempotency_key: IdempotencyKey,
        body: T,
    ) -> Result<Self, EnvelopeError> {
        if idempotency_key.as_str().is_empty() {
            return Err(EnvelopeError::EmptyIdempotencyKey);
        }
        if idempotency_key.as_str().len() > MAX_IDEMPOTENCY_KEY_BYTES {
            return Err(EnvelopeError::IdempotencyKeyTooLong);
        }
        Ok(Self {
            operation_id,
            service_generation,
            task_revision,
            deadline,
            idempotency_key,
            body,
        })
    }

    /// Reuses checked metadata while changing the typed body.
    pub fn with_body<U>(&self, body: U) -> OperationEnvelope<U> {
        OperationEnvelope {
            operation_id: self.operation_id.clone(),
            service_generation: self.service_generation,
            task_revision: self.task_revision,
            deadline: self.deadline,
            idempotency_key: self.idempotency_key.clone(),
            body,
        }
    }
}

/// Why envelope metadata was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EnvelopeError {
    /// The operation identity was empty.
    EmptyOperationId,
    /// The operation identity crossed its boundary cap.
    OperationIdTooLong,
    /// The idempotency key was empty.
    EmptyIdempotencyKey,
    /// The idempotency key crossed its boundary cap.
    IdempotencyKeyTooLong,
}

impl EnvelopeError {
    /// A compiled-in diagnostic label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::EmptyOperationId => "empty_operation_id",
            Self::OperationIdTooLong => "operation_id_too_long",
            Self::EmptyIdempotencyKey => "empty_idempotency_key",
            Self::IdempotencyKeyTooLong => "idempotency_key_too_long",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Deadline;

    #[test]
    fn a_deadline_expires_at_its_exact_boundary() {
        let deadline = Deadline::from_millis(100);
        assert!(!deadline.is_expired_at(99));
        assert!(deadline.is_expired_at(100));
    }
}
