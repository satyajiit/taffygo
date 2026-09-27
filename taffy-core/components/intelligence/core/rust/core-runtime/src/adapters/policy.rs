// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical stateless policy adapter over browser-owned authority facts.

use policy_engine::{GrantDecision, GrantPolicy, GrantRequest, PolicyVersion};
use std::rc::Rc;
use task_engine::{Authorization, CapabilityId, Denial, ProposalDecision};

use super::ids::{DerivedPolicyIds, GenerationEntropy};
use crate::account::Sha256Port;
use crate::contract::ServiceGeneration;
use crate::ports::{PolicyEvaluation, PolicyPort, PortError};
use crate::product_capabilities::ACTIVE;

/// Policy bundle version compiled into the current product graph.
pub const COMPILED_POLICY_VERSION: u32 = 1;

/// Production policy has no Rust lease registry or capability ledger.
#[derive(Clone, Debug)]
pub struct ProductionPolicy {
    policy: Option<GrantPolicy<DerivedPolicyIds>>,
    generation: ServiceGeneration,
}

impl ProductionPolicy {
    /// Builds the one stateless grant minter for a profile generation.
    pub fn new(
        entropy: &GenerationEntropy,
        generation: ServiceGeneration,
        digest: Rc<dyn Sha256Port>,
    ) -> Self {
        Self {
            policy: ACTIVE.policy_milestone.map(|milestone| {
                GrantPolicy::new(
                    DerivedPolicyIds::new(entropy, generation, digest),
                    milestone,
                    PolicyVersion(COMPILED_POLICY_VERSION),
                )
            }),
            generation,
        }
    }
}

impl PolicyPort for ProductionPolicy {
    fn policy_version(&self) -> PolicyVersion {
        PolicyVersion(COMPILED_POLICY_VERSION)
    }

    fn decide(
        &mut self,
        request: &GrantRequest,
        now_millis: u64,
    ) -> Result<PolicyEvaluation, PortError> {
        if request.service_generation != self.generation.value() {
            return Err(PortError::Rejected);
        }
        let Some(policy) = self.policy.as_mut() else {
            return Err(PortError::Rejected);
        };
        let result = policy.decide(request, bip_types::identity::MonotonicMillis(now_millis));
        Ok(match result {
            GrantDecision::Authorize(grant) => PolicyEvaluation {
                task_decision: ProposalDecision::Authorize(Authorization {
                    capability_id: CapabilityId::new(grant.capability_id.as_str()),
                }),
                minted_grant: Some(*grant),
            },
            GrantDecision::RequireApproval => PolicyEvaluation {
                task_decision: ProposalDecision::RequireApproval,
                minted_grant: None,
            },
            GrantDecision::Deny(denial) => PolicyEvaluation {
                task_decision: ProposalDecision::Deny(Denial::new(denial.code)),
                minted_grant: None,
            },
        })
    }
}
