// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Domain-separated identifier derivation from browser entropy.

use core::fmt::Write as _;
use std::rc::Rc;

use bip_types::identity::TaskId;
use policy_engine::time::{IdKind as PolicyIdKind, IdSource as PolicyIdSource};
use task_engine::{IdKind as TaskIdKind, IdSource as TaskIdSource};

use crate::account::Sha256Port;
use crate::contract::ServiceGeneration;
use crate::ports::{PortError, TaskIdEntropy};

/// Bytes of generation-local entropy required for non-restorable grants.
pub const GENERATION_ENTROPY_BYTES: usize = 32;

/// Browser-generated entropy valid for exactly one core service generation.
#[derive(Clone, PartialEq, Eq)]
pub struct GenerationEntropy([u8; GENERATION_ENTROPY_BYTES]);

impl GenerationEntropy {
    /// Accepts a non-degenerate fixed-size entropy value.
    pub fn new(bytes: [u8; GENERATION_ENTROPY_BYTES]) -> Result<Self, PortError> {
        let Some(first) = bytes.first().copied() else {
            return Err(PortError::InvalidInput);
        };
        if bytes.iter().all(|byte| *byte == first) {
            return Err(PortError::InvalidInput);
        }
        Ok(Self(bytes))
    }
}

impl core::fmt::Debug for GenerationEntropy {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str("GenerationEntropy(redacted)")
    }
}

/// Replay-stable source scoped to one task and its persisted entropy.
#[derive(Clone)]
pub struct DerivedTaskIds {
    seed: [u8; 32],
    task_id: String,
    plan: u64,
    plan_step: u64,
    action: u64,
    artifact: u64,
    digest: Rc<dyn Sha256Port>,
}

impl DerivedTaskIds {
    pub(super) fn new(
        task_id: &TaskId,
        entropy: &TaskIdEntropy,
        digest: Rc<dyn Sha256Port>,
    ) -> Self {
        Self {
            seed: *entropy.as_bytes(),
            task_id: task_id.as_str().to_owned(),
            plan: 0,
            plan_step: 0,
            action: 0,
            artifact: 0,
            digest,
        }
    }
}

impl core::fmt::Debug for DerivedTaskIds {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("DerivedTaskIds")
            .field("task_id", &self.task_id)
            .finish_non_exhaustive()
    }
}

impl TaskIdSource for DerivedTaskIds {
    fn next_id(&mut self, kind: TaskIdKind) -> Option<String> {
        let counter = match kind {
            TaskIdKind::Plan => &mut self.plan,
            TaskIdKind::PlanStep => &mut self.plan_step,
            TaskIdKind::Action => &mut self.action,
            TaskIdKind::Artifact => &mut self.artifact,
        };
        let ordinal = *counter;
        *counter = counter.checked_add(1)?;
        derive_id(
            self.digest.as_ref(),
            &self.seed,
            b"taffy/task-id/v1",
            kind.label(),
            self.task_id.as_bytes(),
            ordinal,
        )
    }
}

/// Generation-local source used only by stateless grant minting.
#[derive(Clone)]
pub struct DerivedPolicyIds {
    seed: [u8; 32],
    generation: ServiceGeneration,
    lease: u64,
    capability: u64,
    approval: u64,
    digest: Rc<dyn Sha256Port>,
}

impl DerivedPolicyIds {
    pub(super) fn new(
        entropy: &GenerationEntropy,
        generation: ServiceGeneration,
        digest: Rc<dyn Sha256Port>,
    ) -> Self {
        Self {
            seed: entropy.0,
            generation,
            lease: 0,
            capability: 0,
            approval: 0,
            digest,
        }
    }
}

impl core::fmt::Debug for DerivedPolicyIds {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("DerivedPolicyIds")
            .field("generation", &self.generation)
            .finish_non_exhaustive()
    }
}

impl PolicyIdSource for DerivedPolicyIds {
    fn next_id(&mut self, kind: PolicyIdKind) -> Option<String> {
        let counter = match kind {
            PolicyIdKind::ActorLease => &mut self.lease,
            PolicyIdKind::Capability => &mut self.capability,
            PolicyIdKind::Approval => &mut self.approval,
        };
        let ordinal = *counter;
        *counter = counter.checked_add(1)?;
        derive_id(
            self.digest.as_ref(),
            &self.seed,
            b"taffy/policy-id/v1",
            kind.label(),
            &self.generation.value().to_be_bytes(),
            ordinal,
        )
    }
}

fn derive_id(
    digest_port: &dyn Sha256Port,
    seed: &[u8; 32],
    domain: &[u8],
    prefix: &str,
    context: &[u8],
    ordinal: u64,
) -> Option<String> {
    let mut input = Vec::with_capacity(
        seed.len()
            .saturating_add(domain.len())
            .saturating_add(prefix.len())
            .saturating_add(context.len())
            .saturating_add(core::mem::size_of::<u64>()),
    );
    input.extend_from_slice(domain);
    input.extend_from_slice(seed);
    input.extend_from_slice(prefix.as_bytes());
    input.extend_from_slice(context);
    input.extend_from_slice(&ordinal.to_be_bytes());
    let digest = digest_port.sha256(&input).ok()?;
    let mut encoded = String::with_capacity(prefix.len().saturating_add(65));
    if write!(&mut encoded, "{prefix}_").is_err() {
        return None;
    }
    for byte in digest {
        if write!(&mut encoded, "{byte:02x}").is_err() {
            return None;
        }
    }
    Some(encoded)
}

#[cfg(test)]
mod tests {
    use super::{DerivedPolicyIds, DerivedTaskIds, GenerationEntropy};
    use crate::account::crypto::ReferenceSha256;
    use crate::contract::ServiceGeneration;
    use crate::ports::TaskIdEntropy;
    use bip_types::identity::TaskId;
    use policy_engine::time::{IdKind as PolicyIdKind, IdSource as _};
    use std::rc::Rc;
    use task_engine::{IdKind as TaskIdKind, IdSource as _};

    fn bytes(offset: u8) -> [u8; 32] {
        core::array::from_fn(|index| u8::try_from(index).unwrap_or_default().wrapping_add(offset))
    }

    #[test]
    fn task_ids_replay_with_the_same_seed_and_diverge_with_another() {
        let task = TaskId::new("task-opaque");
        let first_seed = TaskIdEntropy::new(bytes(1)).unwrap_or_else(|_| unreachable!());
        let second_seed = TaskIdEntropy::new(bytes(2)).unwrap_or_else(|_| unreachable!());
        let digest = Rc::new(ReferenceSha256);
        let mut first = DerivedTaskIds::new(&task, &first_seed, digest.clone());
        let mut replay = DerivedTaskIds::new(&task, &first_seed, digest.clone());
        let mut different = DerivedTaskIds::new(&task, &second_seed, digest);
        assert_eq!(
            first.next_id(TaskIdKind::Action),
            replay.next_id(TaskIdKind::Action)
        );
        assert_ne!(
            first.next_id(TaskIdKind::Action),
            different.next_id(TaskIdKind::Action)
        );
    }

    #[test]
    fn capability_ids_are_generation_bound() {
        let entropy = GenerationEntropy::new(bytes(3)).unwrap_or_else(|_| unreachable!());
        let digest = Rc::new(ReferenceSha256);
        let mut first = DerivedPolicyIds::new(&entropy, ServiceGeneration::INITIAL, digest.clone());
        let mut next = DerivedPolicyIds::new(&entropy, ServiceGeneration::new(2), digest);
        assert_ne!(
            first.next_id(PolicyIdKind::Capability),
            next.next_id(PolicyIdKind::Capability)
        );
    }
}
