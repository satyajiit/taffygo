// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::cell::RefCell;
use std::rc::Rc;

use bip_types::identity::{ApprovalReceiptReference, ProfileId, TaskId};
use model_router::catalog::Endpoint;
use model_router::route::RouteRequest;
use model_router::{CredentialRef, RoutePlan, RouteRefusal, TaskLedger};
use policy_engine::GrantRequest;
use task_engine::{
    CommandEnvelope, ControlMode, Effect, IdempotencyKey, PolicyVersion, ScopePreview, SourceScope,
    TaskBudgets, TaskJournal, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId, WorkspaceId,
};

use super::{
    BeginSubmit, CommitCompletionError, CommitOutcome, CoreRuntime, OpenTaskCommitOutcome,
    OpenTaskError, SubmitError, TaskCompletionError, MAX_TASK_SESSIONS_PER_PROFILE,
};
use crate::contract::{
    BoundedPayload, CancellationReason, Completion, Deadline, EffectRequest, OperationEnvelope,
    OperationId, PayloadLimit, RuntimeError, ServiceGeneration,
};
use crate::ports::{
    AuditPort, InitialConsentAdmission, ModelRouterPort, OpenedTaskEngine, PolicyEvaluation,
    PolicyPort, PortError, StorageCommit, StorageDomainPort, TaskCreationAudit, TaskCreationCommit,
    TaskEngineFactory, TaskEngineLoad, TaskIdEntropy, WorkspacePort,
};
use crate::{GenerationJitter, ProductionAccount, ProductionAssetDelivery, ProductionWorkspaces};

use port_doubles::FakeTask;

use super::ServiceRuntimeComponents;

#[derive(Debug)]
struct FakeFactory {
    revision: u64,
}

impl TaskEngineFactory for FakeFactory {
    fn open(&mut self, load: TaskEngineLoad) -> Result<OpenedTaskEngine, PortError> {
        let initial_effects = match &load {
            TaskEngineLoad::Fresh {
                seed,
                initial_consent,
                ..
            } if initial_consent.preview.source_discovery_enabled
                && initial_consent.preview.sources.is_empty() =>
            {
                vec![Effect::PrepareDiscoveryTab {
                    browser_session_id: seed.snapshot.browser_session_id.clone(),
                    remaining_new_source_cap: initial_consent.preview.new_source_cap,
                }]
            }
            TaskEngineLoad::Fresh { .. } | TaskEngineLoad::Replay { .. } => Vec::new(),
        };
        let task_id = load.task_id().clone();
        let workspace_id = match &load {
            TaskEngineLoad::Fresh { seed, .. } | TaskEngineLoad::Replay { seed, .. } => seed
                .workspace_id
                .unwrap_or_else(|| WorkspaceId::from_bytes([1_u8; 16])),
        };
        Ok(OpenedTaskEngine::new(
            Box::new(FakeTask {
                task_id,
                workspace_id,
                revision: self.revision,
                journal: TaskJournal::new(),
                terminal: false,
            }),
            None,
            initial_effects,
        ))
    }
}

#[derive(Debug)]
struct FakePolicy;

impl PolicyPort for FakePolicy {
    fn policy_version(&self) -> policy_engine::PolicyVersion {
        policy_engine::PolicyVersion(1)
    }

    fn decide(
        &mut self,
        _request: &GrantRequest,
        _now_millis: u64,
    ) -> Result<PolicyEvaluation, PortError> {
        Err(PortError::Rejected)
    }
}

#[derive(Debug)]
struct FakeModels;

impl ModelRouterPort for FakeModels {
    fn route(
        &mut self,
        _request: &RouteRequest,
        _ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        Err(RouteRefusal::ProhibitedMaterial)
    }

    // No route is ever planned here, so no candidate exists to ask about.
    fn context_window(&self, _model: &model_router::ModelKey) -> Option<u64> {
        None
    }

    // This double refuses every route unconditionally, so what is installed
    // cannot change the answer it gives. Discarding both is the honest body;
    // the installers are exercised against the real router instead.
    fn replace_credentials(&mut self, _entries: Vec<CredentialRef>) {}

    fn replace_local_endpoints(&mut self, _endpoints: Vec<Endpoint>) {}

    fn install_catalog(&mut self, _catalog: model_router::MergedCatalog) {}

    fn set_entitlement(&mut self, _entitlement: model_router::route::ManagedEntitlement) {}

    fn set_policy(&mut self, _policy: model_router::route::ModelPolicy) {}
}

#[derive(Clone, Debug)]
struct Encoder {
    calls: Rc<RefCell<Vec<&'static str>>>,
    fail_storage: bool,
}

impl AuditPort for Encoder {
    fn encode_task_creation(
        &self,
        _creation: TaskCreationAudit<'_>,
    ) -> Result<Vec<core_service_types::PersistedAuditRecord>, PortError> {
        self.calls.borrow_mut().push("audit_create");
        Ok(Vec::new())
    }

    fn encode_record(
        &self,
        commit: &StorageCommit<'_>,
    ) -> Result<Vec<core_service_types::PersistedAuditRecord>, PortError> {
        self.calls.borrow_mut().push("audit_encode");
        assert_eq!(commit.effect_intents.len(), 1);
        Ok(Vec::new())
    }
}

impl StorageDomainPort for Encoder {
    fn encode_task_creation(
        &self,
        commit: &TaskCreationCommit<'_>,
        audit_records: &[core_service_types::PersistedAuditRecord],
    ) -> Result<BoundedPayload, PortError> {
        self.calls.borrow_mut().push("storage_create");
        assert!(audit_records.is_empty());
        assert!(!commit.seed.task_id.as_str().is_empty());
        payload(b"transactional-create")
    }

    fn encode_commit(
        &self,
        commit: &StorageCommit<'_>,
        audit_records: &[core_service_types::PersistedAuditRecord],
    ) -> Result<BoundedPayload, PortError> {
        self.calls.borrow_mut().push("storage_encode");
        assert_eq!(
            commit.previous_revision.saturating_add(1),
            commit.resulting_revision
        );
        assert!(audit_records.is_empty());
        if self.fail_storage {
            return Err(PortError::Conflict);
        }
        payload(b"transactional-commit")
    }
}

type Runtime = CoreRuntime;

fn payload(bytes: &[u8]) -> Result<BoundedPayload, PortError> {
    let limit = PayloadLimit::new(128).map_err(|_| PortError::InvalidInput)?;
    BoundedPayload::new(bytes.to_vec(), limit).map_err(|_| PortError::InvalidInput)
}

fn runtime(fail_storage: bool, calls: &Rc<RefCell<Vec<&'static str>>>) -> Runtime {
    let mut runtime = empty_runtime(fail_storage, calls);
    must_open(&mut runtime, "task-1");
    calls.borrow_mut().clear();
    runtime
}

fn empty_runtime(fail_storage: bool, calls: &Rc<RefCell<Vec<&'static str>>>) -> Runtime {
    empty_runtime_with_task_revision(fail_storage, calls, 0)
}

fn empty_runtime_with_task_revision(
    fail_storage: bool,
    calls: &Rc<RefCell<Vec<&'static str>>>,
    task_revision: u64,
) -> Runtime {
    let encoder = Encoder {
        calls: Rc::clone(calls),
        fail_storage,
    };
    CoreRuntime::new(
        ServiceGeneration::INITIAL,
        ServiceRuntimeComponents {
            digest: Rc::new(crate::account::crypto::ReferenceSha256),
            task_factory: Box::new(FakeFactory {
                revision: task_revision,
            }),
            policy: Box::new(FakePolicy),
            audit: Box::new(encoder.clone()),
            models: Box::new(FakeModels),
            storage: Box::new(encoder),
            account: Box::new(ProductionAccount::new()),
            workspaces: Box::new(ProductionWorkspaces::new()),
            library: Box::new(crate::ProductionLibrary::new(false)),
            memory: Box::new(crate::ProductionMemory::new(false)),
            assets: Box::new(ProductionAssetDelivery::new(Box::new(
                GenerationJitter::new([0u8; 32]),
            ))),
            observers: Vec::new(),
        },
    )
}

fn must_open(runtime: &mut Runtime, value: &str) {
    open_load(runtime, value, load(value));
}

fn must_open_with_workspace(runtime: &mut Runtime, value: &str, workspace_byte: u8) {
    open_load(runtime, value, load_with_workspace(value, workspace_byte));
}

fn open_load(runtime: &mut Runtime, value: &str, load: TaskEngineLoad) {
    let task_id = TaskId::new(value);
    let begin = runtime.begin_open_task(
        load,
        operation_id(&format!("open-{value}")),
        Deadline::from_millis(100),
        1,
        1,
    );
    let commit = match begin {
        Ok(value) => value.commit,
        Err(_) => unreachable!(),
    };
    let terminal = storage_success(&commit);
    assert!(matches!(
        runtime.complete_open_task(&task_id, terminal, 2),
        Ok(OpenTaskCommitOutcome::Opened(_))
    ));
}

fn storage_success(commit: &OperationEnvelope<EffectRequest>) -> OperationEnvelope<Completion> {
    let revision = match &commit.body {
        EffectRequest::Storage(effect) => effect.resulting_revision(),
        _ => unreachable!(),
    };
    commit.with_body(Completion::StorageCommitted {
        committed_revision: revision,
    })
}

fn load(task_id: &str) -> TaskEngineLoad {
    let task_id_bytes = task_id.as_bytes();
    TaskEngineLoad::fresh(
        TaskSeed {
            task_id: TaskId::new(task_id),
            workspace_id: None,
            browser_profile_id: ProfileId::new("profile-1"),
            kind: task_engine::TaskKind::Research,
            user_goal: "redacted test goal".to_owned(),
            control_mode: ControlMode::Assistant,
            snapshot: TaskSnapshot {
                template_id: TaskTemplateId::CompareProducts,
                assistant_config_version: 1,
                skill_version_id: None,
                builtin_skill: None,
                tool_allowlist: Vec::new(),
                capability_policy_version: PolicyVersion(1),
                provider_route: None,
                consented_sources: Vec::new(),
                source_discovery_enabled: false,
                remaining_new_source_cap: 0,
                discovery_tab_id: None,
                library_refresh: None,
                browser_session_id: task_engine::BrowserSessionId::new("browser-session-1")
                    .unwrap_or_else(|_| unreachable!()),
                milestone: task_engine::Milestone::M3,
            },
            budgets: TaskBudgets::none(),
            deadline: None,
            deadline_utc: None,
            predecessor_task_id: None,
        },
        IdempotencyKey::new(format!("create-{task_id}")),
        TraceId::new(format!("trace-{task_id}")),
        TaskIdEntropy::new(core::array::from_fn(|index| {
            task_id_bytes[index % task_id_bytes.len()] ^ u8::try_from(index).unwrap_or_default()
        }))
        .unwrap_or_else(|_| unreachable!()),
        InitialConsentAdmission {
            preview: ScopePreview {
                scope: SourceScope::new(),
                sources: Vec::new(),
                source_discovery_enabled: false,
                new_source_cap: 0,
                provider_route: None,
                budgets: TaskBudgets::none(),
            },
            receipt: ApprovalReceiptReference::new("initial-consent"),
            start_key: IdempotencyKey::new(format!("initial-start-{task_id}")),
            start_trace: TraceId::new(format!("initial-start-trace-{task_id}")),
            consent_key: IdempotencyKey::new(format!("initial-accept-{task_id}")),
            consent_trace: TraceId::new(format!("initial-accept-trace-{task_id}")),
        },
    )
}

fn load_with_workspace(task_id: &str, workspace_byte: u8) -> TaskEngineLoad {
    let mut load = load(task_id);
    let TaskEngineLoad::Fresh { seed, .. } = &mut load else {
        unreachable!("load constructs a fresh task")
    };
    seed.workspace_id = Some(WorkspaceId::from_bytes([workspace_byte; 16]));
    load
}

fn discovery_load(task_id: &str) -> TaskEngineLoad {
    let mut load = load(task_id);
    let TaskEngineLoad::Fresh {
        seed,
        initial_consent,
        ..
    } = &mut load
    else {
        unreachable!("load constructs a fresh task")
    };
    seed.snapshot.template_id = TaskTemplateId::WebErrand;
    seed.snapshot.milestone = task_engine::Milestone::M5;
    seed.budgets = TaskBudgets::none()
        .with(task_engine::BudgetKind::MaxSources, 4)
        .with(task_engine::BudgetKind::MaxModelRequests, 64);
    initial_consent.preview.source_discovery_enabled = true;
    initial_consent.preview.new_source_cap = 4;
    initial_consent.preview.budgets = seed.budgets.clone();
    load
}

fn task_id() -> TaskId {
    TaskId::new("task-1")
}

fn command() -> CommandEnvelope {
    CommandEnvelope::new(
        IdempotencyKey::new("command-1"),
        0,
        TraceId::new("trace-1"),
        task_engine::Command::CreateTask,
    )
}

fn operation_id(value: &str) -> OperationId {
    OperationId::new(value).unwrap_or_else(|_| unreachable!())
}

fn begin_for(
    runtime: &mut Runtime,
    task_id: &TaskId,
    operation: &str,
) -> crate::contract::OperationEnvelope<crate::contract::EffectRequest> {
    match runtime.begin_submit(
        task_id,
        command(),
        operation_id(operation),
        Deadline::from_millis(100),
        1,
        1,
    ) {
        Ok(BeginSubmit::AwaitingCommit(effect)) => *effect,
        Ok(BeginSubmit::Duplicate(_)) | Err(_) => unreachable!(),
    }
}

/// Records every settled fact so a test can assert observation is post-hoc.
struct RecordingObserver {
    seen: Rc<RefCell<Vec<(String, String, u64)>>>,
}

impl crate::ports::TurnObserverPort for RecordingObserver {
    fn observe(&mut self, fact: &crate::ports::SettledFact<'_>) {
        match fact {
            crate::ports::SettledFact::CommandCommitted {
                task_id,
                to,
                revision,
                ..
            } => self
                .seen
                .borrow_mut()
                .push(((*task_id).to_owned(), format!("{to:?}"), *revision)),
        }
    }
}

fn runtime_observed(
    calls: &Rc<RefCell<Vec<&'static str>>>,
    observer: Box<dyn crate::ports::TurnObserverPort>,
) -> Runtime {
    let mut runtime = empty_runtime(false, calls);
    runtime.components.observers.push(observer);
    must_open(&mut runtime, "task-1");
    calls.borrow_mut().clear();
    runtime
}

mod cases;
mod loop_state;
mod port_doubles;
mod substitution;
