// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Pure projection from facts the ordered runtime actually owns.

use core_api_types::{
    AuthAccountView, AuthFailure, AuthMethodAvailability, AuthMethodView, AuthPhase, AuthProvider,
    AuthViewState, CoreAvailability, CoreStatus, TaskViewState, WorkspaceDeletionPreviewView,
    MAX_ACTIVE_TASKS,
};

use crate::account::{AccountAuthMethod, AccountFailure, AccountFailureCode};
use crate::ports::{AccountPort, TaskViewFactsError, WorkspaceStoreError};

use super::types::TaskSession;
use super::CoreRuntime;

mod asset_projection;
mod browser_bindings;
mod library_projection;
mod task_projection;

use self::asset_projection::project_asset_delivery;
use self::browser_bindings::{project_task_browser_bindings, TaskBrowserBindings};
use self::library_projection::project_library_view;
use self::task_projection::project_task_view;

/// A required Core API fact absent from the canonical runtime.
///
/// Callers must withhold the state update instead of replacing the gap with a
/// default, fixture, or empty projection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CoreStatusProjectionGap {
    /// Canonical reducer facts disagreed with one another.
    TaskFactsInconsistent {
        task_id: String,
        reason: TaskViewFactsError,
    },
    /// A task goal cannot fit the stricter UI boundary.
    GoalTooLarge { task_id: String },
    /// Plan progress cannot be represented truthfully.
    InvalidProgress { task_id: String },
    /// One task exceeded the generated artifact-metadata collection bound.
    TooManyTaskArtifacts { task_id: String },
    /// One task's record of what it did arrived over the generated bound.
    ///
    /// The reducer drops from the front at [`task_engine::MAX_TASK_ACTIVITY`],
    /// so this is unreachable while the two bounds agree — which is exactly why
    /// it is a named gap rather than a truncation: a list that is somehow longer
    /// than the contract admits is a disagreement between two files, and a
    /// surface silently shown the first thirty-two of it would never say so.
    TooManyActivitySteps { task_id: String },
    /// The resident task set exceeded the generated Core API bound.
    TooManyTasks,
    /// Canonical workspace facts could not fit the generated Core API view.
    WorkspaceProjection(WorkspaceStoreError),
}

/// Browser/service-private correlation for one visible pending approval.
///
/// This value is transported beside, never inside, the generated Core API
/// status payload.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingApprovalBindingFacts {
    pub task_id: String,
    pub action_id: String,
    pub proposal_digest: String,
    pub task_revision: u64,
}

/// Browser/service-private exact native permission request.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingPermissionBindingFacts {
    pub task_id: String,
    pub request_id: String,
    pub permission: task_engine::PlatformPermission,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

/// Browser/service-private current revision and controls for one active task.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskRevisionBindingFacts {
    pub task_id: String,
    pub task_revision: u64,
    pub allowed_controls: Vec<task_engine::TaskControlKind>,
}

/// Browser/service-private settlement requested by one durable task state.
///
/// The browser acknowledges revocation and cancellation of task-owned work
/// before the service submits the matching internal settlement command.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskSettlementBindingFacts {
    pub task_id: String,
    pub task_revision: u64,
    pub kind: TaskSettlementKind,
}

/// Closed internal settlement families; never exposed through Core API.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskSettlementKind {
    Pause,
    Cancel,
}

/// Closed terminal task states used only for browser authority cleanup.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerminalTaskKind {
    Completed,
    Partial,
    Failed,
    Cancelled,
}

/// Browser/service-private durable terminal fact.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TerminalTaskBindingFacts {
    pub task_id: String,
    pub task_revision: u64,
    pub kind: TerminalTaskKind,
}

/// Browser-only durable initial consent, never included in Core API payloads.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AcceptedTaskConsentBindingFacts {
    pub task_id: String,
    pub current_task_revision: u64,
    pub accepted_revision: u64,
    pub browser_session_id: String,
    pub receipt_id: String,
    pub sources: Vec<task_engine::ConsentedSource>,
    pub source_discovery_enabled: bool,
    pub new_source_cap: u32,
    pub provider_route_id: Option<String>,
}

/// Browser-only durable and unconsumed one-use action approval.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommittedActionApprovalBindingFacts {
    pub task_id: String,
    pub action_id: String,
    pub committed_revision: u64,
    pub receipt_id: String,
    pub proposal_digest: String,
    pub expires_at_monotonic_ms: u64,
    pub expires_at_utc_ms: u64,
    pub browser_session_id: String,
}

/// One immutable status projection and its browser-only approval bindings.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CoreStatusProjection {
    pub status: CoreStatus,
    pub task_revisions: Vec<TaskRevisionBindingFacts>,
    pub pending_approvals: Vec<PendingApprovalBindingFacts>,
    pub pending_permissions: Vec<PendingPermissionBindingFacts>,
    pub task_settlements: Vec<TaskSettlementBindingFacts>,
    pub terminal_tasks: Vec<TerminalTaskBindingFacts>,
    pub accepted_task_consents: Vec<AcceptedTaskConsentBindingFacts>,
    pub committed_action_approvals: Vec<CommittedActionApprovalBindingFacts>,
}

/// Projects the account state screen SCR-701 renders.
///
/// Every field here is a fact the runtime holds. The four that used to be
/// hardcoded absent were not simplifications: `methods` empty meant the surface
/// could never tell an unconfigured method from a working one, and `failure`
/// absent made `AuthPhase::Failed` unreachable, so a cancelled sheet and a
/// refused provider both rendered as plain signed-out and six translated
/// failure sentences could not be reached by any input.
pub fn project_account_view(account: &dyn AccountPort) -> AuthViewState {
    let methods = project_auth_methods(account.available_methods(), account.last_failure());
    if let Some(session) = account.session() {
        return AuthViewState {
            phase: AuthPhase::SignedIn,
            account: Some(AuthAccountView {
                account_id: session.account_subject.as_str().to_owned(),
                display_name: session
                    .display_name
                    .as_ref()
                    .map(|name| name.as_str().to_owned()),
                email: session
                    .email
                    .as_ref()
                    .map(|email| email.as_str().to_owned()),
                method: project_auth_method(session.auth_method),
            }),
            // A signed-in profile has no link outstanding: sending one requires
            // no session, and storing a session clears the marker.
            pending_email: None,
            failure: None,
            methods,
            // The entitlement is profile state, not runtime state; the
            // profile's entitlement contributor fills this (decision 0073).
            entitlement: None,
        };
    }
    let pending_email = account
        .pending_email()
        .map(|email| email.as_str().to_owned());
    let failure = account.last_failure().map(|failure| AuthFailure {
        code: failure.code.to_wire(),
        retryable: failure.retryable(),
    });
    // A sent link is tested before an in-flight flow rather than after it,
    // because it is one: the protocol holds the redirect open for exactly as
    // long as "check your email" is true, so `pending_count()` is non-zero the
    // whole time and reading it first would make `LINK_SENT` unreachable — the
    // precise defect this projection exists to remove.
    //
    // The last two cannot both apply. Recording a failure clears a sent link
    // and sending a link clears a recorded failure, so the protocol never
    // holds both at once.
    let phase = if pending_email.is_some() {
        AuthPhase::LinkSent
    } else if account.pending_count() != 0 {
        AuthPhase::InFlight
    } else if failure.is_some() {
        AuthPhase::Failed
    } else {
        AuthPhase::SignedOut
    };
    AuthViewState {
        phase,
        account: None,
        pending_email,
        failure,
        methods,
        // Left absent here for the same reason as above; a signed-out profile
        // additionally has no summary for the contributor to project.
        entitlement: None,
    }
}

/// One row per method this product draws, in the contract's own order.
///
/// The list is the four the screen offers, never the subset that happens to
/// work: a method the browser did not report is `NOT_CONFIGURED`, which is a
/// state SCR-701 can explain, rather than an absent row, which it cannot.
///
/// A method the browser announced but that then refused to start is demoted to
/// the same state, because the announcement is a build-time fact and the
/// refusal is a runtime observation of the same thing. The two agree today —
/// the browser announces Google only when the server client id validates, and
/// that is the same condition the broker refuses on — so this fires only when
/// they disagree, and when they disagree the flow that actually ran is the one
/// telling the truth. Only `NOT_CONFIGURED` demotes: every other reason is
/// about one attempt rather than about whether the method exists.
fn project_auth_methods(
    available: &[AccountAuthMethod],
    failure: Option<&AccountFailure>,
) -> Vec<AuthMethodView> {
    let unconfigured = failure.and_then(|failure| {
        (failure.code == AccountFailureCode::NotConfigured).then_some(failure.method)?
    });
    [
        (AuthProvider::Google, AccountAuthMethod::Google),
        (AuthProvider::EmailLink, AccountAuthMethod::EmailLink),
        (AuthProvider::Github, AccountAuthMethod::Github),
        (AuthProvider::Facebook, AccountAuthMethod::Facebook),
    ]
    .into_iter()
    .map(|(provider, method)| AuthMethodView {
        provider,
        availability: if available.contains(&method) && unconfigured != Some(method) {
            AuthMethodAvailability::Available
        } else {
            AuthMethodAvailability::NotConfigured
        },
    })
    .collect()
}

const fn project_auth_method(method: AccountAuthMethod) -> AuthProvider {
    match method {
        AccountAuthMethod::Google => AuthProvider::Google,
        AccountAuthMethod::EmailLink => AuthProvider::EmailLink,
        AccountAuthMethod::Github => AuthProvider::Github,
        AccountAuthMethod::Facebook => AuthProvider::Facebook,
    }
}

impl CoreRuntime {
    fn project_workspace_views(
        &self,
    ) -> Result<Vec<core_api_types::WorkspaceViewState>, WorkspaceStoreError> {
        let mut workspaces = self.components.workspaces.project_core_api()?;
        for workspace in &mut workspaces {
            if !workspace.saved {
                workspace.deletion_preview = None;
                continue;
            }
            let preview = self
                .components
                .workspaces
                .preview_deletion(&workspace.workspace_id, self.components.digest.as_ref())?;
            if preview.expected_revision != workspace.revision {
                return Err(WorkspaceStoreError::StaleRevision);
            }
            workspace.deletion_preview = Some(WorkspaceDeletionPreviewView {
                sources: preview.counts.sources,
                facts: preview.counts.facts,
                artifact_metadata: preview.counts.artifact_metadata,
                derived_indexes: preview.counts.derived_indexes,
                confirmation_token: preview.confirmation_token,
            });
        }
        Ok(workspaces)
    }

    /// Builds one immutable Core API state only when every required fact is
    /// present. Availability is supplied by the process supervisor; generation
    /// and account/task facts come from this ordered profile runtime.
    pub fn project_core_status(
        &self,
        availability: CoreAvailability,
    ) -> Result<CoreStatus, CoreStatusProjectionGap> {
        self.project_core_status_with_approvals(availability)
            .map(|projection| projection.status)
    }

    /// Projects status and exact browser-only approval bindings from the same
    /// immutable reducer facts.
    pub fn project_core_status_with_approvals(
        &self,
        availability: CoreAvailability,
    ) -> Result<CoreStatusProjection, CoreStatusProjectionGap> {
        let TaskProjections {
            active_tasks,
            bindings,
        } = self.project_tasks()?;
        // The ceiling is on what crosses the wire, not on what the runtime
        // holds. It used to be the second, and that made a bound into a cliff:
        // the profile that had run its sixty-fifth task ever projected no state
        // at all, so every core-backed surface went empty for the life of it.
        if active_tasks.len() > MAX_ACTIVE_TASKS {
            return Err(CoreStatusProjectionGap::TooManyTasks);
        }
        let TaskBindings {
            task_revisions,
            pending_approvals,
            pending_permissions,
            task_settlements,
            terminal_tasks,
            accepted_task_consents,
            committed_action_approvals,
        } = bindings;
        let workspaces = self
            .project_workspace_views()
            .map_err(CoreStatusProjectionGap::WorkspaceProjection)?;
        Ok(CoreStatusProjection {
            status: CoreStatus {
                availability,
                generation: self.service_generation().value(),
                active_tasks,
                auth_state: Some(project_account_view(self.components.account.as_ref())),
                workspaces,
                workspace_export: self.components.workspaces.latest_export(),
                asset_delivery: project_asset_delivery(
                    self.components.assets.posture(),
                    self.components.assets.installations(),
                ),
                // The provider plane is profile state, not runtime state; the
                // profile's roster contributor fills this (decision 0073).
                provider_roster: Vec::new(),
                // Probe verdicts are profile state too; the profile's probe
                // contributor fills this (decision 0083).
                provider_probes: Vec::new(),
                // The models a person picks from are the merged catalog's,
                // which the provider plane holds; the profile's model
                // contributor fills this (decision 0093).
                provider_models: Vec::new(),
                assistant_configuration: core_api_types::AssistantConfigurationView {
                    revision: 0,
                    disabled_abilities: Vec::new(),
                    preset: core_api_types::PersonalityPresetView::CarefulResearcher,
                    pace: 0,
                    length: 1,
                    check_in: 0,
                },
                library: project_library_view(self),
                library_export: self.components.library.latest_export(),
                memory: self.components.memory.project_core_api(),
                saved_sign_ins: core_api_types::SavedSignInsView {
                    availability: core_api_types::SavedDataAvailability::Loading,
                    revision: 0,
                    records: Vec::new(),
                },
                saved_details: core_api_types::SavedDetailsView {
                    availability: core_api_types::SavedDataAvailability::Loading,
                    revision: 0,
                    people: Vec::new(),
                },
                site_skills: Vec::new(),
                builtin_skills: Vec::new(),
                projection_mode: core_api_types::CoreStatusProjectionMode::Complete,
                projection_omissions: Vec::new(),
            },
            task_revisions,
            pending_approvals,
            pending_permissions,
            task_settlements,
            terminal_tasks,
            accepted_task_consents,
            committed_action_approvals,
        })
    }

    /// Every task the runtime holds, projected once: the views the surfaces
    /// read, and the bindings the browser routes by.
    ///
    /// **One pass over every session, and the narrowing happens inside it.**
    /// The browser looks a restored task effect up in `task_revisions`, so a
    /// session left out of this walk is a core that will not start —
    /// `[taffy_core_state_projection_refused] at=task-effect/unknown-task`,
    /// which is what a phone answered when decision 0149's filter was applied
    /// one step too early, before the bindings were built rather than after.
    fn project_tasks(&self) -> Result<TaskProjections, CoreStatusProjectionGap> {
        let sessions = sessions_in_recency_order(&self.tasks);
        let mut active_tasks = Vec::with_capacity(sessions.len());
        let mut bindings = TaskBindings::with_capacity(sessions.len());
        for session in sessions {
            let task_id = session.task.task_id().as_str().to_owned();
            let facts = session.task.view_facts().map_err(|reason| {
                CoreStatusProjectionGap::TaskFactsInconsistent { task_id, reason }
            })?;
            let binding = project_task_browser_bindings(&facts);
            let task = project_task_view(facts)?;
            bindings.push(&task, binding);
            // The one narrowing, and the only one: a task that came back from
            // the journal already over is not this browser run's news, and no
            // surface is told about it (decision 0149).
            if !session.terminal_on_restore {
                active_tasks.push(task);
            }
        }
        Ok(TaskProjections {
            active_tasks,
            bindings,
        })
    }
}

#[cfg(test)]
mod tests;

/// Every session, newest first.
fn sessions_in_recency_order(
    tasks: &std::collections::BTreeMap<String, TaskSession>,
) -> Vec<&TaskSession> {
    let sessions: Vec<&TaskSession> = tasks.values().collect();
    let keys: Vec<RecencyKey<'_>> = sessions
        .iter()
        .map(|session| RecencyKey {
            updated_at_millis: session.task.updated_at_utc_millis(),
            task_id: session.task.task_id().as_str(),
        })
        .collect();
    recency_order(&keys)
        .into_iter()
        .filter_map(|index| sessions.get(index).copied())
        .collect()
}

/// What the order is decided on, and nothing else about a task.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct RecencyKey<'a> {
    /// When the aggregate last changed, in milliseconds since the Unix epoch.
    pub(super) updated_at_millis: u64,
    /// The identity, which breaks ties so the order is total.
    pub(super) task_id: &'a str,
}

/// The order the tasks are projected in: newest first.
///
/// Split from the sessions themselves because it is the whole of what could be
/// wrong here and the only part a host can run: a `TaskSession` owns a boxed
/// reducer, and this is two fields and a comparison.
///
/// A revision counts one task's own transitions and says nothing across two of
/// them, and the runtime holds its sessions in a map keyed by identity — so
/// "the most recent task" was whichever identity sorted first, which is not a
/// fact about recency at all. `updated_at` is, and ties break on identity so
/// the order is total and stable across a rebuild.
///
/// **It orders and never omits.** Which of these a *surface* is told about is a
/// separate question, decided one step later, because every session projected
/// here also produces the browser bindings that let a restored effect find its
/// task. Omitting one here is a core that will not start (decision 0149).
pub(super) fn recency_order(keys: &[RecencyKey<'_>]) -> Vec<usize> {
    let mut rows: Vec<(usize, &RecencyKey<'_>)> = keys.iter().enumerate().collect();
    rows.sort_by(|(_, left), (_, right)| {
        right
            .updated_at_millis
            .cmp(&left.updated_at_millis)
            .then_with(|| left.task_id.cmp(right.task_id))
    });
    rows.into_iter().map(|(index, _)| index).collect()
}

/// What one pass over the sessions produces.
struct TaskProjections {
    /// The tasks the surfaces are told about, newest first.
    active_tasks: Vec<TaskViewState>,
    /// The browser's own routing facts, for every session without exception.
    bindings: TaskBindings,
}

/// The seven browser-only vectors, gathered in one place so the walk that fills
/// them stays one loop rather than seven.
#[derive(Debug, Default)]
struct TaskBindings {
    task_revisions: Vec<TaskRevisionBindingFacts>,
    pending_approvals: Vec<PendingApprovalBindingFacts>,
    pending_permissions: Vec<PendingPermissionBindingFacts>,
    task_settlements: Vec<TaskSettlementBindingFacts>,
    terminal_tasks: Vec<TerminalTaskBindingFacts>,
    accepted_task_consents: Vec<AcceptedTaskConsentBindingFacts>,
    committed_action_approvals: Vec<CommittedActionApprovalBindingFacts>,
}

impl TaskBindings {
    fn with_capacity(sessions: usize) -> Self {
        Self {
            task_revisions: Vec::with_capacity(sessions),
            ..Self::default()
        }
    }

    fn push(&mut self, task: &TaskViewState, binding: TaskBrowserBindings) {
        self.task_revisions.push(TaskRevisionBindingFacts {
            task_id: task.task_id.clone(),
            task_revision: task.revision,
            allowed_controls: binding.allowed_controls,
        });
        if let Some(approval) = binding.pending_approval {
            self.pending_approvals.push(approval);
        }
        if let Some(permission) = binding.pending_permission {
            self.pending_permissions.push(permission);
        }
        if let Some(settlement) = binding.settlement {
            self.task_settlements.push(settlement);
        }
        if let Some(terminal) = binding.terminal {
            self.terminal_tasks.push(terminal);
        }
        if let Some(consent) = binding.accepted_consent {
            self.accepted_task_consents.push(consent);
        }
        self.committed_action_approvals
            .extend(binding.committed_approvals);
    }
}
