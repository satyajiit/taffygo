// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the runtime must do after the reducer commits (decision 0004).
//!
//! The reducer performs nothing. It returns an ordered list of effects, and the
//! order is part of the contract:
//!
//! **Authority is revoked before anything waits on remote work.** Entering
//! `PAUSING` or `CANCELLING` means the assistant stops being allowed to act
//! immediately and locally — not once a renderer answers, not once a provider
//! request finishes, not once a tab settles. [`revocation_precedes_remote_wait`]
//! is that sentence as a predicate, and the settling constructors build lists
//! that satisfy it by construction rather than by convention.
//!
//! Handing the page to a person is the same sentence with the sharpest case
//! behind it. [`hand_over`] revokes first and waits second, because the tab it
//! is about is one somebody is going to type into.

use bip_types::identity::{ActionId, SemanticNodeId, TabId};

use crate::artifact::ArtifactKind;
use crate::authority::RevocationReason;
use crate::command::CommandKind;
use crate::field_values::{FieldNodeIds, FieldValueRequestId};
use crate::handover::{HandoverId, HANDOVER_WINDOW_MS};
use crate::ids::{ArtifactId, ModelCallId};
use crate::permission::{PermissionRequestId, PlatformPermission};
use crate::tool::RecoveryRule;

/// One thing the runtime has to do.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Effect {
    /// Revoke the task's leases and every capability issued under them that
    /// has not been dispatched. Local, synchronous, and complete when it
    /// returns.
    RevokeAuthority {
        /// Why authority ended.
        reason: RevocationReason,
    },
    /// Ask `policy-engine` to decide a proposal. Local and synchronous.
    AskPolicy {
        /// Which action.
        action_id: ActionId,
    },
    /// Show the user an approval request built from a trusted local template.
    RequestApproval {
        /// Which action.
        action_id: ActionId,
    },
    /// Ask the browser to prepare one task-owned blank/search tab for a
    /// zero-source Web errand.
    ///
    /// The effect carries no source and grants no page authority. The browser
    /// returns the exact tab identity through `RecordDiscoveryTab`; only a
    /// later verified action outcome may contribute a source binding.
    PrepareDiscoveryTab {
        /// Browser-process session in which the tab identity must be minted.
        browser_session_id: crate::BrowserSessionId,
        /// Remaining discovery ceiling frozen into this exact effect.
        remaining_new_source_cap: u32,
    },
    /// Open one closed native permission surface. The result returns through a
    /// separate exact reducer command and grants no browser authority.
    RequestPermission {
        request_id: PermissionRequestId,
        permission: PlatformPermission,
        deadline_monotonic_ms: u64,
        deadline_utc_ms: u64,
        browser_session_id: crate::BrowserSessionId,
    },
    /// Journal the intent and send the action to the browser broker.
    DispatchAction {
        /// Which action.
        action_id: ActionId,
    },
    /// Journal the intent and hand one job to the tool broker.
    ///
    /// The dispatched half of a proposal whose class is
    /// [`crate::authority::ActionClass::ExecuteToolJob`]: same authority
    /// spend, same one-attempt accounting, different performer. The job
    /// reaches an isolated runtime the core supervises, never a page, and its
    /// one completion returns as [`crate::Command::RecordToolJobOutcome`].
    RunToolJob {
        /// Which action.
        action_id: ActionId,
        /// The job identity, derived from the task and the action so a
        /// replay reaches the same one and the effect journal can refuse a
        /// job it has already run.
        job_id: crate::ids::ToolJobId,
        /// Which isolated runtime family performs it.
        runtime: crate::tool::ToolRuntime,
    },
    /// Run one profile-local Library operation in the core. A write may stage
    /// one browser-owned atomic storage effect before its action settles.
    RunLibraryTool {
        /// Which exact authorized action owns the operation and any result.
        action_id: ActionId,
    },
    /// Run one profile-local Memory operation in the core. Writes have already
    /// received exact person approval and remain invisible until browser
    /// storage commits their exact revisions.
    RunMemoryTool { action_id: ActionId },
    /// Give the page to the person, and wait for them.
    ///
    /// The assistant is holding nothing by the time this runs: [`hand_over`]
    /// puts the revocation first, and the reason it does is not tidiness. A
    /// handover exists because the person is about to type something into a
    /// tab — a test that proves they are present, a code that arrived on
    /// another device — and an assistant that still held mutation authority
    /// over that tab could act in the middle of it. Authority ends locally and
    /// synchronously; only then does anything wait.
    ///
    /// The effect carries the handover's identity and how long the window
    /// stays open, and nothing about *why* the assistant stopped in a form the
    /// browser could act on.
    ///
    /// The product does now classify a challenge (decision 0088), but that
    /// classification is a hint the *surface* uses and never reaches here:
    /// handover stays the floor everything falls back to, and a floor that
    /// read a detector's output would be a floor with a detector's false
    /// negatives in it. See [`crate::handover`].
    AwaitHandover {
        /// Which handover.
        handover_id: HandoverId,
        /// How long the window stays open, in milliseconds.
        window_ms: u32,
    },
    /// Ask the person to fill in a form's fields, and wait (decision 0088).
    ///
    /// Unlike [`Self::AwaitHandover`] this revokes nothing: the person types
    /// into a surface Taffy draws rather than acting in the page, so the
    /// task's lease stands and the assistant carries on in the same tab once
    /// the values are held.
    ///
    /// It names the form and never its fields — or, when the model named a
    /// field rather than a form, that field and the page's other fields only
    /// the person can supply (decision 0238). Which of them need a person,
    /// and what kind of thing each one is, is the browser's judgement — it
    /// re-reads the classification from the node and believes nothing this
    /// process asserted about it.
    RequestFieldValues {
        /// Which request.
        request_id: FieldValueRequestId,
        /// The tab the form is in.
        tab_id: TabId,
        /// The form the person is being asked to fill in.
        node_id: SemanticNodeId,
        /// The other fields on the same page only the person can supply,
        /// in page order, when `node_id` names a field rather than a form
        /// (decision 0238). The browser re-reads each and asks only about
        /// those that still need a person.
        companion_node_ids: FieldNodeIds,
    },
    /// Wait for work already in flight to settle, then send `settle_with`.
    AwaitInFlightWork {
        /// The command the runtime sends once everything has settled.
        settle_with: CommandKind,
    },
    /// Reconcile an attempt whose outcome could not be confirmed.
    ReconcileAction {
        /// Which action.
        action_id: ActionId,
        /// What the tool's idempotency class permits.
        rule: RecoveryRule,
    },
    /// Close the tabs the task opened.
    ReleaseTaskTabs,
    /// Generate an artifact from the accepted facts. Local and deterministic.
    GenerateArtifact {
        /// Which accepted artifact record these bytes belong to.
        artifact_id: ArtifactId,
        /// Which format.
        kind: ArtifactKind,
        /// Exact workspace revision to reproduce.
        workspace_revision: u64,
    },
    /// Hand the artifact to the system document picker.
    ExportArtifact {
        /// Which accepted artifact is being exported.
        artifact_id: ArtifactId,
        /// Which format.
        kind: ArtifactKind,
        /// Exact workspace revision to reproduce and publish.
        workspace_revision: u64,
    },
    /// Ask the model, and bring back one typed completion.
    ///
    /// The **browser process** performs the request, because that is where the
    /// network stack, the credential store and the URL loader are, and because
    /// a sandboxed utility process has no business holding a provider key
    /// (decision 0052 section 2). The core never sees a credential, never
    /// composes a URL host, and never learns whether the transport retried.
    ///
    /// The effect carries the call identity and nothing else. The request it
    /// stands for holds the rendered page, and the journal records that a paid
    /// call was *claimed* rather than what was sent — which is what lets the
    /// browser refuse a second delivery of an identity it has already spent
    /// without the journal holding a line of the page.
    CallModel {
        /// Which call.
        call_id: ModelCallId,
    },
}

impl Effect {
    /// Whether this effect ends authority.
    pub const fn revokes_authority(&self) -> bool {
        matches!(self, Self::RevokeAuthority { .. })
    }

    /// Whether this effect waits on work outside the ordered core sequence.
    ///
    /// Asking policy and generating an artifact are local and synchronous, so
    /// neither waits. Dispatching, settling in-flight work, reconciling an
    /// unknown outcome, handing a file to another application, opening a
    /// platform permission surface, waiting for a person, and asking a model
    /// all do — the last one most of all, because it leaves the device, and
    /// the person most of all in time, because a person is under no obligation
    /// to come back.
    pub const fn waits_on_remote_work(&self) -> bool {
        matches!(
            self,
            Self::DispatchAction { .. }
                | Self::PrepareDiscoveryTab { .. }
                | Self::RunToolJob { .. }
                | Self::RunLibraryTool { .. }
                | Self::RunMemoryTool { .. }
                | Self::AwaitHandover { .. }
                | Self::RequestFieldValues { .. }
                | Self::AwaitInFlightWork { .. }
                | Self::ReconcileAction { .. }
                | Self::ExportArtifact { .. }
                | Self::RequestPermission { .. }
                | Self::CallModel { .. }
        )
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::RevokeAuthority { .. } => "revoke_authority",
            Self::AskPolicy { .. } => "ask_policy",
            Self::RequestApproval { .. } => "request_approval",
            Self::PrepareDiscoveryTab { .. } => "prepare_discovery_tab",
            Self::RequestPermission { .. } => "request_permission",
            Self::AwaitHandover { .. } => "await_handover",
            Self::RequestFieldValues { .. } => "request_field_values",
            Self::DispatchAction { .. } => "dispatch_action",
            Self::RunToolJob { .. } => "run_tool_job",
            Self::RunLibraryTool { .. } => "run_library_tool",
            Self::RunMemoryTool { .. } => "run_memory_tool",
            Self::AwaitInFlightWork { .. } => "await_in_flight_work",
            Self::ReconcileAction { .. } => "reconcile_action",
            Self::ReleaseTaskTabs => "release_task_tabs",
            Self::GenerateArtifact { .. } => "generate_artifact",
            Self::ExportArtifact { .. } => "export_artifact",
            Self::CallModel { .. } => "call_model",
        }
    }
}

/// The effects of settling towards a held or stopped task.
///
/// Revocation first, always. The constructor exists so no call site has to
/// remember the order.
pub fn settle(reason: RevocationReason, settle_with: CommandKind) -> Vec<Effect> {
    vec![
        Effect::RevokeAuthority { reason },
        Effect::AwaitInFlightWork { settle_with },
    ]
}

/// The effects of handing the page to the person.
///
/// Revocation first, and here the order carries more than the general rule.
/// [`RevocationReason::UserTookOver`] is the honest reason — from this moment
/// the tab is the person's — and it is emitted **before** the wait rather than
/// alongside it or after it, because the assistant must not hold mutation
/// authority over a tab a person is typing into. Emitting the wait first would
/// leave a window, however short, in which a queued action could still land on
/// the field the person had just focused.
///
/// The constructor exists so that no call site can get that order wrong, and
/// [`revocation_precedes_remote_wait`] is the predicate that says it did not.
pub fn hand_over(handover_id: HandoverId) -> Vec<Effect> {
    vec![
        Effect::RevokeAuthority {
            reason: RevocationReason::UserTookOver,
        },
        Effect::AwaitHandover {
            handover_id,
            window_ms: HANDOVER_WINDOW_MS,
        },
    ]
}

/// Whether every remote wait in `effects` comes after authority was revoked.
///
/// True when no effect revokes authority and none waits either — a list that
/// does neither cannot get the order wrong. False as soon as something waits on
/// remote work while authority is still standing.
pub fn revocation_precedes_remote_wait(effects: &[Effect]) -> bool {
    let first_revocation = effects.iter().position(Effect::revokes_authority);
    let first_wait = effects.iter().position(Effect::waits_on_remote_work);
    match (first_revocation, first_wait) {
        (Some(revoked), Some(waited)) => revoked < waited,
        (Some(_) | None, None) => true,
        (None, Some(_)) => false,
    }
}

#[cfg(test)]
mod tests {
    use super::{hand_over, revocation_precedes_remote_wait, settle, Effect};
    use crate::authority::RevocationReason;
    use crate::command::CommandKind;
    use crate::handover::{handover_id_for_call, HANDOVER_WINDOW_MS};
    use bip_types::identity::ActionId;

    #[test]
    fn handing_over_revokes_before_it_waits() {
        let effects = hand_over(handover_id_for_call(1, 0));
        assert!(revocation_precedes_remote_wait(&effects));
        assert_eq!(
            effects.first(),
            Some(&Effect::RevokeAuthority {
                reason: RevocationReason::UserTookOver
            }),
            "the person has the tab before anything waits on them"
        );
        assert_eq!(
            effects.get(1),
            Some(&Effect::AwaitHandover {
                handover_id: handover_id_for_call(1, 0),
                window_ms: HANDOVER_WINDOW_MS,
            })
        );
    }

    #[test]
    fn a_handover_that_waited_first_is_rejected() {
        let effects = vec![
            Effect::AwaitHandover {
                handover_id: handover_id_for_call(1, 0),
                window_ms: HANDOVER_WINDOW_MS,
            },
            Effect::RevokeAuthority {
                reason: RevocationReason::UserTookOver,
            },
        ];
        assert!(!revocation_precedes_remote_wait(&effects));
    }

    #[test]
    fn settling_revokes_before_it_waits() {
        for (reason, settle_with) in [
            (RevocationReason::UserTookOver, CommandKind::PauseSettled),
            (RevocationReason::TaskCancelled, CommandKind::CancelSettled),
        ] {
            let effects = settle(reason, settle_with);
            assert!(revocation_precedes_remote_wait(&effects));
            assert!(effects
                .first()
                .is_some_and(super::Effect::revokes_authority));
        }
    }

    #[test]
    fn a_list_that_waits_before_revoking_is_rejected() {
        let effects = vec![
            Effect::AwaitInFlightWork {
                settle_with: CommandKind::PauseSettled,
            },
            Effect::RevokeAuthority {
                reason: RevocationReason::UserTookOver,
            },
        ];
        assert!(!revocation_precedes_remote_wait(&effects));
    }

    #[test]
    fn asking_policy_is_not_a_remote_wait() {
        let effects = vec![Effect::AskPolicy {
            action_id: ActionId::new("act_0"),
        }];
        assert!(!effects
            .first()
            .is_some_and(super::Effect::waits_on_remote_work));
        assert!(revocation_precedes_remote_wait(&effects));
    }

    #[test]
    fn a_dispatch_with_no_revocation_is_not_a_settling_list() {
        let effects = vec![Effect::DispatchAction {
            action_id: ActionId::new("act_0"),
        }];
        assert!(!revocation_precedes_remote_wait(&effects));
    }
}
