// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Recording stand-ins for the two subsystems that used to be concrete state.
//!
//! They exist to prove substitutability: nothing about the account protocol or
//! the workspace plane is reachable from the runtime except through its port.

use std::cell::RefCell;
use std::rc::Rc;

use crate::account::{
    AccountAuthMethod, AccountEffect, AccountEmail, AccountError, AccountFailure, AccountSession,
    AuthFlowId, AuthorizationEntropy, AuthorizationIntent, GoogleNonceEntropy,
    NativeCredentialOutcome, RedirectReceipt, SecretHandle, SessionReceipt, Sha256Port,
};
use crate::contract::Deadline;
use bip_types::identity::TaskId;
use task_engine::{
    Accepted, CommandEnvelope, Effect, PolicyVersion, Refusal, RefusalReason, TaskJournal,
    TaskState, TaskTemplateId, WorkspaceId,
};

use crate::ports::{AccountPort, TaskEnginePort, TaskViewFacts, TaskViewFactsError};

mod workspace;

pub(super) use workspace::FakeWorkspaces;

pub(super) const SUBSTITUTED_WORKSPACE: &str = "substituted-workspace";

/// A recording account plane that is signed out with one flow in flight.
#[derive(Debug, Default)]
pub(super) struct FakeAccount {
    pub(super) calls: Rc<RefCell<Vec<&'static str>>>,
    pub(super) session: Option<AccountSession>,
    pub(super) pending: usize,
    pub(super) available_methods: Vec<AccountAuthMethod>,
    pub(super) last_failure: Option<AccountFailure>,
    pub(super) pending_email: Option<AccountEmail>,
}

impl FakeAccount {
    fn record(&self, name: &'static str) {
        self.calls.borrow_mut().push(name);
    }

    fn refuse(&self, name: &'static str) -> AccountError {
        self.record(name);
        AccountError::UnknownFlow
    }
}

impl AccountPort for FakeAccount {
    fn session(&self) -> Option<&AccountSession> {
        self.record("session");
        self.session.as_ref()
    }

    fn pending_count(&self) -> usize {
        self.record("pending_count");
        self.pending
    }

    fn available_methods(&self) -> &[AccountAuthMethod] {
        self.record("available_methods");
        &self.available_methods
    }

    fn last_failure(&self) -> Option<&AccountFailure> {
        self.record("last_failure");
        self.last_failure.as_ref()
    }

    fn pending_email(&self) -> Option<&AccountEmail> {
        self.record("pending_email");
        self.pending_email.as_ref()
    }

    fn set_available_methods(&mut self, methods: Vec<AccountAuthMethod>) {
        self.record("set_available_methods");
        self.available_methods = methods;
    }

    fn reconciliation_required(&self) -> bool {
        false
    }

    fn restore_session(&mut self, session: Option<AccountSession>) {
        self.record("restore_session");
        self.session = session;
    }

    fn begin_authorization(
        &mut self,
        _intent: AuthorizationIntent,
        _now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("begin_authorization"))
    }

    fn begin_native_authorization(
        &mut self,
        _flow_id: AuthFlowId,
        _auth_method: AccountAuthMethod,
        _deadline: Deadline,
        _now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("begin_native_authorization"))
    }

    fn accept_authorization_entropy(
        &mut self,
        _flow_id: &AuthFlowId,
        _entropy: &AuthorizationEntropy,
        _digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_authorization_entropy"))
    }

    fn accept_pkce_verifier_handle(
        &mut self,
        _flow_id: &AuthFlowId,
        _handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_pkce_verifier_handle"))
    }

    fn accept_google_nonce_entropy(
        &mut self,
        _flow_id: &AuthFlowId,
        _entropy: &GoogleNonceEntropy,
        _digest_port: &dyn Sha256Port,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_google_nonce_entropy"))
    }

    fn accept_google_raw_nonce_handle(
        &mut self,
        _flow_id: &AuthFlowId,
        _raw_nonce_handle: SecretHandle,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_google_raw_nonce_handle"))
    }

    fn accept_redirect(
        &mut self,
        _receipt: &RedirectReceipt,
        _now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_redirect"))
    }

    fn accept_authorization_surface(
        &mut self,
        _flow_id: &AuthFlowId,
        _opened: bool,
    ) -> Result<(), AccountError> {
        Err(self.refuse("accept_authorization_surface"))
    }

    fn accept_email_link_delivery(
        &mut self,
        _flow_id: &AuthFlowId,
        _accepted: bool,
    ) -> Result<(), AccountError> {
        Err(self.refuse("accept_email_link_delivery"))
    }

    fn accept_native_credential(
        &mut self,
        _flow_id: &AuthFlowId,
        _auth_method: AccountAuthMethod,
        _outcome: NativeCredentialOutcome,
        _now_millis: u64,
    ) -> Result<AccountEffect, AccountError> {
        Err(self.refuse("accept_native_credential"))
    }

    fn accept_exchange(
        &mut self,
        _flow_id: &AuthFlowId,
        _receipt: SessionReceipt,
    ) -> Result<AccountSession, AccountError> {
        Err(self.refuse("accept_exchange"))
    }

    fn begin_refresh(&mut self, _now_millis: u64) -> Result<Option<AccountEffect>, AccountError> {
        self.record("begin_refresh");
        Ok(None)
    }

    fn accept_refresh(&mut self, _receipt: SessionReceipt) -> Result<AccountSession, AccountError> {
        Err(self.refuse("accept_refresh"))
    }

    fn settle_refresh_failure(&mut self, _outcome_unknown: bool) -> bool {
        self.record("settle_refresh_failure");
        false
    }

    fn begin_sign_out(&mut self) -> Result<Option<AccountEffect>, AccountError> {
        self.record("begin_sign_out");
        Ok(None)
    }

    fn cancel_flow(&mut self, _flow_id: &AuthFlowId, _failure: AccountFailure) -> bool {
        self.record("cancel_flow");
        false
    }

    fn require_reconciliation(&mut self) {
        self.record("require_reconciliation");
    }
}

#[derive(Debug)]
pub(super) struct FakeTask {
    pub(super) task_id: TaskId,
    pub(super) workspace_id: WorkspaceId,
    pub(super) revision: u64,
    pub(super) journal: TaskJournal,
    /// Whether this stand-in reports itself as over (decision 0149).
    pub(super) terminal: bool,
}

impl TaskEnginePort for FakeTask {
    fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    fn workspace_id(&self) -> Option<&WorkspaceId> {
        Some(&self.workspace_id)
    }

    fn revision(&self) -> u64 {
        self.revision
    }

    fn updated_at_utc_millis(&self) -> u64 {
        self.revision
    }

    fn is_terminal(&self) -> bool {
        self.terminal
    }

    fn capability_policy_version(&self) -> PolicyVersion {
        PolicyVersion(1)
    }

    fn action_effect_facts(
        &self,
        _action_id: &bip_types::identity::ActionId,
    ) -> Option<crate::ports::ActionEffectFacts> {
        None
    }

    fn journal(&self) -> &TaskJournal {
        &self.journal
    }

    fn view_facts(&self) -> Result<TaskViewFacts, TaskViewFactsError> {
        Ok(TaskViewFacts {
            task_id: self.task_id.as_str().to_owned(),
            revision: self.revision,
            state: TaskState::Draft,
            execution_phase: None,
            goal: "redacted test goal".to_owned(),
            template_id: TaskTemplateId::CompareProducts,
            workspace_id: Some(self.workspace_id.to_text()),
            plan_progress: None,
            terminal_failure: None,
            pause_cause: None,
            model_attempt: None,
            last_move_refused: false,
            reply_being_reasked: false,
            pending_action: None,
            pending_permission: None,
            accepted_consent: Some(crate::ports::AcceptedTaskConsentFacts {
                accepted_revision: 0,
                browser_session_id: "browser-session-1".to_owned(),
                receipt_id: "initial-consent".to_owned(),
                sources: Vec::new(),
                source_discovery_enabled: false,
                new_source_cap: 0,
                provider_route_id: None,
            }),
            committed_action_approvals: Vec::new(),
            outcome_unknown_actions: 0,
            artifacts: Vec::new(),
            activity: Vec::new(),
            allowed_controls: vec![task_engine::TaskControlKind::Stop],
            pending_handover: None,
            pending_field_values: None,
        })
    }

    fn next_reviewed_command(
        &self,
        _digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::WorkflowError> {
        Ok(None)
    }

    fn refused_on_sight(
        &self,
        _residency: &task_engine::TurnResidency,
    ) -> Vec<(u32, task_engine::NotAttempted)> {
        Vec::new()
    }

    fn model_turn_facts(&self) -> crate::ports::ModelTurnFacts {
        crate::ports::ModelTurnFacts {
            task_id: self.task_id.as_str().to_owned(),
            attempts_started: 1,
            candidate_ordinal: 0,
            can_afford_model_attempt: true,
            transcript: crate::context::TaskTranscript::new(
                "redacted test goal".to_owned(),
                Vec::new(),
                crate::context::TranscriptBudget::default(),
            ),
            provider_route_id: None,
            tool_allowlist: Vec::new(),
            milestone: task_engine::Milestone::M3,
            template_id: task_engine::TaskTemplateId::SummarizeEvidence,
            source_count: 0,
            remaining_new_source_cap: 0,
            empty_page_tab_id: None,
            discovery_tab_id: None,
            persons_pages: task_engine::PersonsPages::default(),
            activated: Vec::new(),
            nested_goal: None,
            thinking: None,
        }
    }

    fn model_turn_in_flight(&self) -> Option<String> {
        None
    }

    fn next_agent_command(
        &self,
        _residency: Option<&task_engine::TurnResidency>,
        _digest: &dyn task_engine::WorkflowDigest,
    ) -> Result<Option<task_engine::Command>, task_engine::AgentError> {
        Ok(None)
    }

    fn apply(&mut self, envelope: CommandEnvelope) -> Result<Accepted, Refusal> {
        if envelope.expected_revision != self.revision {
            return Err(Refusal {
                reason: RefusalReason::RevisionConflict,
                state: TaskState::Draft,
                command: envelope.kind(),
                revision: self.revision,
            });
        }
        let kind = envelope.kind();
        let trace_id = envelope.trace_id.clone();
        let causation_key = envelope.idempotency_key.clone();
        self.journal
            .append_command(envelope, task_engine::UtcMillis(1));
        let event = task_engine::TaskEvent::record(task_engine::EventKind::TaskCreated, kind);
        self.revision = self.journal.append_event(
            event.clone(),
            trace_id,
            causation_key,
            task_engine::UtcMillis(1),
        );
        Ok(Accepted {
            from: TaskState::Draft,
            to: TaskState::Draft,
            revision: self.revision,
            events: vec![event],
            effects: vec![Effect::ReleaseTaskTabs],
            duplicate: false,
        })
    }
}
