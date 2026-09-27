// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading one call of a reply, and turning the one that is next into a
//! proposal.
//!
//! # The order of the checks is the design
//!
//! A call is read against the registry, then against the compiled-in argument
//! schema, then against the numbers this task issued, then against the
//! repetition register, and only then against what the build can actually
//! serve. Each step answers with a compiled-in [`NotAttempted`] reason and
//! none of them repairs anything: a name is never matched to its nearest
//! registered neighbour, a value is never coerced to the declared type, and a
//! number that names nothing is never resolved to the node it probably meant.
//! Every one of those repairs is the step that turns a model's mistake into a
//! plausible-looking call, and a plausible-looking call is the one that passes
//! every later check.
//!
//! # What a proposal carries comes from the table
//!
//! The action class and the idempotency class are read from
//! [`crate::tool::ToolEntry`]. Nothing the reply said about itself reaches
//! them. `Guard::ToolAvailable` re-derives the same two facts when the command
//! is applied and refuses a proposal that disagrees, so this module cannot
//! widen anything even by being wrong.

use bip_types::identity::{ContentDigest, DigestAlgorithm, SemanticNodeId};

use super::browser_intent::{browser_intent, tool_job_intent, BrowserIntentContext};
use super::library_intent::library_intent;
use super::memory_intent::memory_intent;
use super::proposal::proposal_material;
use super::reply::{CallDisposition, CallVerdict, ModelToolCall, NotAttempted, TurnResidency};
use super::store_intent::store_intent;
use super::target::{call_target, named_number};
use super::turn::{ModelStopReason, ModelTurn};
use super::AgentError;
use crate::action::ActionProposal;
use crate::command::Command;
use crate::field_values::{field_value_request_id_for_call, FieldNodeIds};
use crate::handover::handover_id_for_call;
use crate::ids::{IdSource, IdempotencyKey};
use crate::proposal::hex_digest;
use crate::reducer::Reducer;
use crate::time::Clock;
use crate::tool::{self, ToolDispatch};
use crate::workflow::WorkflowDigest;

/// The idempotency key of the `sequence`-th call of turn `ordinal`.
///
/// The two numbers are the whole identity, and neither is a position in a
/// batch that could be re-planned: a turn ordinal only ever increases, and a
/// reply is frozen the moment it is read. The *n*-th call of the *m*-th turn
/// of one task therefore happens exactly once, which is what
/// `Guard::ProposalNotAlreadyDispatched` and the browser's effect-identity
/// journal both need.
pub(super) fn turn_call_key(ordinal: u64, sequence: u32) -> IdempotencyKey {
    IdempotencyKey::new(format!("turn-{ordinal}-call-{sequence}"))
}

/// The turn and the call one key names, when it names one.
///
/// The inverse of [`turn_call_key`], and it lives beside it because the format
/// has one owner. A second reader that knew how to take the string apart would
/// be a second place the format is written down, and the two would agree right
/// up until one of them was edited — at which point a transcript would be
/// assembled from turns that never happened, or from none.
///
/// Strict rather than lenient: both halves must parse whole, so a key from any
/// other minting answers `None` rather than a turn. A plan step's
/// `{namespace}-{source}-{step}-{attempt}` is the one that exists today, and a
/// lenient reader would find a turn in the next one somebody adds.
pub fn turn_call_of(key: &IdempotencyKey) -> Option<(u64, u32)> {
    let rest = key.as_str().strip_prefix("turn-")?;
    let (ordinal, sequence) = rest.split_once("-call-")?;
    Some((ordinal.parse().ok()?, sequence.parse().ok()?))
}

/// Replay-stable artifact identity for one call in one task.
///
/// Scoped by the task aggregate, like action identities. It names the frozen
/// turn and sequence rather than a batch position that could be reused.
pub fn artifact_id_for_call(ordinal: u64, sequence: u32) -> crate::ids::ArtifactId {
    crate::ids::ArtifactId::new(format!("artifact-turn-{ordinal}-call-{sequence}"))
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// What the reducer will do with each call of `residency`'s reply, in the
    /// order the provider declared them.
    ///
    /// Pure, and the counts in [`super::TurnDigest`] are derived from it, so
    /// the durable record and the decision cannot disagree about how many
    /// calls were refused on sight.
    ///
    /// Two rules shape the list, and both are about calls the model *ordered*:
    ///
    /// - **A `Length` ending refuses every call.** The reply was cut off at
    ///   the token allowance, so the argument list is a prefix of an intention
    ///   rather than a call anybody made. Running the ones that happen to have
    ///   parsed would execute part of a sentence.
    /// - **The first refusal ends the list.** A call refused at position *k*
    ///   makes every call after it [`NotAttempted::PriorCallRefused`], because
    ///   the model ordered them and a later call may depend on an earlier one
    ///   having happened.
    pub fn turn_dispositions(&self, residency: &TurnResidency) -> Vec<CallDisposition> {
        let calls = &residency.reply().tool_calls;
        let truncated = residency.reply().stop == ModelStopReason::Length;
        let mut dispositions = Vec::with_capacity(calls.len());
        let mut refused = false;
        for (index, call) in calls.iter().enumerate() {
            let sequence = u32::try_from(index).unwrap_or(u32::MAX);
            let verdict = if truncated {
                CallVerdict::NotAttempted(NotAttempted::TruncatedArguments)
            } else if refused {
                CallVerdict::NotAttempted(NotAttempted::PriorCallRefused)
            } else {
                self.read_call(residency, call)
            };
            refused = refused || !verdict.is_attemptable();
            dispositions.push(CallDisposition { sequence, verdict });
        }
        dispositions
    }

    fn read_call(&self, residency: &TurnResidency, call: &ModelToolCall) -> CallVerdict {
        let milestone = self.task().snapshot().milestone;
        let lookup = tool::resolve(&call.tool_name, milestone);
        let Some(entry) = lookup.entry().filter(|_| lookup.is_available()) else {
            return CallVerdict::NotAttempted(NotAttempted::ToolNotAvailable);
        };
        if !self.admits_tool(&call.tool_name) {
            return CallVerdict::NotAttempted(NotAttempted::ToolNotAvailable);
        }
        if tool::validate(entry.definition(), &call.arguments).is_err()
            || !tool::call_operands_are_valid(entry.name, &call.arguments)
        {
            return CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected);
        }
        if entry.name == "browser.tabs.open" && !call_has_argument(call, "address") {
            return CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected);
        }
        if matches!(entry.name, "browser.form.fill" | "browser.form.select") {
            let Some(index) = call_supplied_value(call, "value_from") else {
                return CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected);
            };
            // A position the person did not supply, or one the task already
            // put into its field, is not the model's to fill (decision 0238).
            if !self.holds_unplaced_value(index) {
                return CallVerdict::NotAttempted(NotAttempted::ArgumentsRejected);
            }
        }
        let target = match call_target(residency, entry, call, &self.persons_pages()) {
            Ok(target) => target,
            Err(reason) => return CallVerdict::NotAttempted(reason),
        };
        if target.task_tab.as_ref().is_some_and(|task_tab| {
            task_tab.browser_session_id() != &self.task().snapshot().browser_session_id
        }) {
            return CallVerdict::NotAttempted(NotAttempted::HandleUnknown);
        }
        let destination = call_address(call, "address");
        // The same fingerprint `Guard::NotLoopingOnRefusals` computes from the
        // committed proposal, so the loop guard and this reading count the
        // same call. A navigation's target is the address, not a node. The
        // escape keeps its exemption here for the reason
        // `tool::UNCONDITIONAL_TOOLS` gives: three refused handovers mean the
        // way out is exactly what is not working, and abandoning it there
        // would leave the task holding the page with nothing left to say.
        let fingerprint = if entry.name == tool::NAVIGATE_TOOL {
            tool::CallFingerprint::of_target(
                &call.tool_name,
                Some(target.tab_id.as_str()),
                destination,
            )
        } else if let Some(task_tab) = target.task_tab.as_ref() {
            tool::CallFingerprint::of_target(
                &call.tool_name,
                Some(target.tab_id.as_str()),
                Some(task_tab.tab_id().as_str()),
            )
        } else if let Some((_, download)) = target.task_download.as_ref() {
            tool::CallFingerprint::of_target(
                &call.tool_name,
                Some(target.tab_id.as_str()),
                Some(download.download_id()),
            )
        } else {
            tool::CallFingerprint::of_target(
                &call.tool_name,
                Some(target.tab_id.as_str()),
                target.node_id.as_ref().map(SemanticNodeId::as_str),
            )
        };
        if !tool::is_unconditional(&call.tool_name) && self.refusals().is_abandoned(fingerprint) {
            return CallVerdict::NotAttempted(NotAttempted::RepeatedRefusalsAbandoned);
        }
        match entry.dispatch {
            ToolDispatch::BrowserAction(_)
            | ToolDispatch::ToolJob(_)
            | ToolDispatch::Library(_)
            | ToolDispatch::Memory(_)
            | ToolDispatch::Artifact(_)
            | ToolDispatch::Person
            | ToolDispatch::Loop => CallVerdict::Attemptable,
            ToolDispatch::Unserved => CallVerdict::NotAttempted(NotAttempted::NoRuntimeHere),
        }
    }

    /// The command that attempts the `sequence`-th call of `turn`.
    ///
    /// A browser call becomes an ordinary proposal that `policy-engine` will
    /// decide. A call that reaches the person becomes the reducer's own
    /// waiting state, spends no capability, and reaches no page.
    pub(super) fn attempt_call(
        &self,
        turn: &ModelTurn,
        residency: &TurnResidency,
        sequence: u32,
        digest: &dyn WorkflowDigest,
    ) -> Result<Command, AgentError> {
        let Some(call) = residency.call(sequence) else {
            return Err(AgentError::ContradictoryReply);
        };
        let milestone = self.task().snapshot().milestone;
        let Some(entry) = tool::resolve(&call.tool_name, milestone).entry() else {
            return Err(AgentError::ContradictoryReply);
        };
        let Ok(target) = call_target(residency, entry, call, &self.persons_pages()) else {
            return Err(AgentError::ContradictoryReply);
        };
        match entry.dispatch {
            ToolDispatch::Artifact(format) => Ok(Command::RequestArtifact {
                artifact_id: artifact_id_for_call(turn.ordinal(), sequence),
                format,
                // The workspace plane binds this candidate to the exact
                // successfully rendered revision before it is submitted.
                workspace_revision: 0,
            }),
            // Both `user.*` rows land here and both put the task in
            // `WAITING_USER`, because that is the one durable state meaning
            // "the assistant has stopped and the person has it". They part
            // company on what happens to authority. Asking for a value leaves
            // the task's lease standing, because the assistant will carry on
            // in the same tab with the same authority once the value arrives.
            // Handing the page back does not: the person is about to act in
            // that tab, so the lease is revoked before anything waits, and the
            // resumption has to acquire a new one — which is
            // `Command::RequestHandover` and the effects it returns.
            //
            // The handover's identity is derived from the turn and the call,
            // never minted, so a replay re-opens the same handover rather than
            // a second one. What the person is shown is composed from a
            // trusted local template in either case; this reducer holds no
            // text to compose it from.
            ToolDispatch::Person => Ok(if entry.name == tool::HANDOVER_TOOL {
                Command::RequestHandover {
                    handover_id: handover_id_for_call(turn.ordinal(), sequence),
                }
            } else if entry.name == "user.request_values" {
                let node_id = target.node_id.ok_or(AgentError::ContradictoryReply)?;
                // A field named on its own brings the page's other fields
                // only the person can supply onto the same sheet, so the
                // person is asked once for an identity number and the
                // CAPTCHA beside it (decision 0238). A block brings every
                // such field of its page: the browser expands a form itself,
                // and asks about these only when the block has no fields of
                // its own (decision 0243).
                let companions = named_number(entry, call).map_or_else(Vec::new, |value| {
                    residency.page().handles().person_only_companions(
                        value,
                        crate::field_values::MAX_REQUESTED_FIELDS as usize - 1,
                    )
                });
                Command::RequestFieldValues {
                    request_id: field_value_request_id_for_call(turn.ordinal(), sequence),
                    tab_id: target.tab_id,
                    companion_node_ids: FieldNodeIds::companions_of(&node_id, companions)
                        .map_err(|_| AgentError::ContradictoryReply)?,
                    node_id,
                }
            } else {
                Command::RequestUserInput
            }),
            // The walk returns `Ok(None)` for an unsettled loop call and
            // treats a settled one as verified. Reaching here would propose a
            // browser effect for a tool that has none.
            ToolDispatch::Loop | ToolDispatch::Unserved => Err(AgentError::ContradictoryReply),
            // A tool job is an ordinary proposal wearing its own class. The
            // dispatch names the class in both arms, so the body is one body:
            // policy decides it, the ledger spends it, and only the effect it
            // becomes at dispatch differs.
            ToolDispatch::BrowserAction(_)
            | ToolDispatch::ToolJob(_)
            | ToolDispatch::Library(_)
            | ToolDispatch::Memory(_) => {
                let context = BrowserIntentContext {
                    turn_ordinal: turn.ordinal(),
                    sequence,
                    residency,
                    target: &target,
                    browser_session_id: &self.task().snapshot().browser_session_id,
                    supplied_field_values: self.supplied_field_values(),
                    digest,
                };
                let intent = Self::proposal_intent(entry, call, &context)?;
                if entry.dispatch.action_class() != Some(intent.action_class())
                    || entry.idempotency != intent.idempotency()
                {
                    return Err(AgentError::ContradictoryReply);
                }
                let key = turn_call_key(turn.ordinal(), sequence);
                let material = proposal_material(
                    self.task().task_id().as_str(),
                    turn.call_id().as_str(),
                    sequence,
                    &intent,
                    &key,
                )?;
                let proposal_digest = ContentDigest {
                    algorithm: DigestAlgorithm::Sha256,
                    value: hex_digest(&digest.sha256(&material).map_err(|_| {
                        // The reviewed workflow's narrow adapter is reused
                        // rather than duplicated; its one failure is that the
                        // adapter was not there, which is this error.
                        AgentError::DigestUnavailable
                    })?),
                };
                Ok(Command::ProposeAction(Box::new(ActionProposal::new(
                    intent,
                    None,
                    key,
                    false,
                    None,
                    proposal_digest,
                ))))
            }
        }
    }

    /// The typed intent one proposal-bearing call becomes, by its row's
    /// dispatch. A store read is a browser action wearing its own class, so it
    /// is matched before the general browser arm.
    fn proposal_intent(
        entry: &'static tool::ToolEntry,
        call: &ModelToolCall,
        context: &BrowserIntentContext<'_>,
    ) -> Result<crate::action::ActionIntent, AgentError> {
        let (turn_ordinal, sequence) = (context.turn_ordinal, context.sequence);
        let (residency, target, digest) = (context.residency, context.target, context.digest);
        match entry.dispatch {
            ToolDispatch::BrowserAction(crate::authority::ActionClass::ProfileStoreRead) => {
                store_intent(call, turn_ordinal, sequence, residency, target, digest)
            }
            ToolDispatch::BrowserAction(_) => browser_intent(call, context),
            ToolDispatch::ToolJob(runtime) => tool_job_intent(entry, runtime, call, context),
            ToolDispatch::Library(_) => {
                library_intent(call, turn_ordinal, sequence, residency, target, digest)
            }
            ToolDispatch::Memory(_) => {
                memory_intent(call, turn_ordinal, sequence, residency, target, digest)
            }
            _ => Err(AgentError::ContradictoryReply),
        }
    }
}

fn call_address<'a>(call: &'a ModelToolCall, name: &str) -> Option<&'a str> {
    call.arguments
        .iter()
        .find_map(|argument| match &argument.value {
            crate::tool::ArgumentValue::Address(value) if argument.name == name => {
                Some(value.as_str())
            }
            _ => None,
        })
}

fn call_supplied_value(call: &ModelToolCall, name: &str) -> Option<u32> {
    call.arguments
        .iter()
        .find_map(|argument| match &argument.value {
            crate::tool::ArgumentValue::SuppliedValue(value) if argument.name == name => {
                Some(*value)
            }
            _ => None,
        })
}

fn call_has_argument(call: &ModelToolCall, name: &str) -> bool {
    call.arguments.iter().any(|argument| argument.name == name)
}

#[cfg(test)]
mod tests;
