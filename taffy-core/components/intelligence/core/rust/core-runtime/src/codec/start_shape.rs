// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which generated start-task shapes this decoder will admit.

use core_service_types as wire;
use task_engine::{ConsentedSource, ProviderRouteId, TaskTemplateId, REVIEWED_OBSERVATION_TOOL};

/// First bound on model turns for a `DirectUserKey` start.
///
/// Keep in step with `kDirectUserKeyMaxModelRequests` in the Core API command
/// factory: that composer states this number, and a command that states any
/// other is not the generated `DirectUserKey` shape.
pub const DIRECT_USER_KEY_MAX_MODEL_REQUESTS: u64 = 32;

/// First bound on model turns for a `ManagedService` start.
///
/// The same number as [`DIRECT_USER_KEY_MAX_MODEL_REQUESTS`] on purpose: this
/// budget exists to stop a runaway loop, the account's real ceiling is the
/// worker's own meter. The Core API command factory now composes this exact
/// selected-page shape, and this decoder independently refuses any other
/// stated bound.
pub const MANAGED_SERVICE_MAX_MODEL_REQUESTS: u64 = 32;
/// Most exact pages one selected-source research task may hold at once.
///
/// The generated contract permits a wider generic list. This is the product
/// shape the transient aggregate and consent screen actually implement: large
/// enough for a comparison, bounded independently of the wire ceiling, and
/// never a discovery grant.
pub const RESEARCH_MAX_SELECTED_SOURCES: usize = 8;

/// First bound on model turns for an errand (decision 0087).
///
/// Larger than the page-scoped bounds above because an errand walks a site
/// rather than reading one document, and a site that asks a person for three
/// things across four pages spends turns proportional to how it was built
/// rather than to what was asked for.
///
/// This is a stop-the-runaway bound and not a measured budget. What a task's
/// transcript may actually cost, and what is dropped first when it cannot, is
/// OD-110 — unresolved and unmeasured. An errand is simply the first workload
/// that will find it, so this number is deliberately high enough to let one
/// complete and low enough that a loop cannot run forever.
///
/// It was 64, and 64 was not high enough to let one complete. On a phone the
/// errand "Download my eAadhaar" searched, read the results, followed a result
/// to the site, reloaded, read again and reached the page that asks for an
/// Aadhaar number — every move verified — on its twenty-fifth proposal, and
/// the budget ran out one move later. That is the errand arriving and then
/// being stopped at the door, reported as though it could not read
/// (decision 0177). Reading a results page and following it costs turns before
/// the errand's own work starts at all, and the errand's own work is where a
/// person is asked for a number, a code and a captcha.
pub const ERRAND_MAX_MODEL_REQUESTS: u64 = 160;

/// How many paid attempts one logical model turn may add after a retryable
/// refusal (decision 0218).
///
/// Keep in step with `kMaxRetriesPerStep` in the Core API command factory, and
/// with `SEMANTIC_RETRY_ATTEMPTS` in the model router, which admits three paid
/// attempts per turn: one call and these two.
///
/// Every start that may call a model states this number, because the task
/// factory fills an unstated budget with zero and a zero here means the retry
/// ladder — backoff, `Retry-After`, failover — is compiled in and unreachable.
/// That is what it was: an errand whose ninth turn was answered `503` by a
/// provider that had answered `200` eight times ended on the first refusal,
/// and the person was told their provider limit was reached.
pub const MAX_RETRIES_PER_STEP: u64 = 2;

/// A start that may make no model request may repeat no paid attempt.
///
/// Stated rather than omitted: this decoder compares the budget list exactly,
/// so an absent entry and a zero one must not read as the same command.
pub const NO_MODEL_MAX_RETRIES_PER_STEP: u64 = 0;

/// The largest number of origins an errand may add beyond those it was given.
///
/// The value is a bound on the *cap a person may consent to*, not a grant: the
/// origins are still granted one at a time, at proposal time, by the policy
/// engine (decision 0033, invariant I-03). What this number stops is a consent
/// surface offering an unbounded errand.
///
/// OD-007 owns the maximum source count per task and now owns it for two
/// shapes rather than one.
pub const ERRAND_MAX_NEW_SOURCE_CAP: u32 = task_engine::MAX_WEB_ERRAND_NEW_SOURCE_CAP;

/// Why a generated start-task record cannot become a reducer seed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StartTaskDecodeError {
    CapabilityProfileDisabled,
    CapabilityProfileMismatch,
    EmptyIdentity,
    IdentityTooLong,
    GoalTooLarge,
    InvalidWorkspaceId,
    InvalidProviderRoute,
    InvalidDeadline,
    InvalidEntropy,
    DuplicateBudget,
    TooManyTools,
    InvalidToolName,
    InvalidCreationRevision,
    InvalidConsentPreview,
    InvalidConsentReceipt,
    InvalidBuiltinSkillBinding,
    ConsentBudgetMismatch,
    ProviderNotConfigured,
    InvalidReviewedWorkflow,
    InvalidAgentWorkflow,
    /// The command claimed the errand shape of decision 0087 and did not have
    /// it. Named separately from `InvalidAgentWorkflow` so a refusal says which
    /// shape was not met.
    InvalidErrandWorkflow,
}

impl StartTaskDecodeError {
    /// A short, compiled-in name for this refusal.
    ///
    /// The wire carries one admission status and nothing else, so a start the
    /// core will not decode reaches the phone as "Taffy could not read this
    /// request" for every one of these reasons at once. The label is the
    /// variant and never a value from the command: it is recorded as the
    /// bridge's last refusal and printed by the browser beside the status.
    pub const fn label(self) -> &'static str {
        match self {
            Self::CapabilityProfileDisabled => "start_capability_disabled",
            Self::CapabilityProfileMismatch => "start_capability_mismatch",
            Self::EmptyIdentity => "start_identity_empty",
            Self::IdentityTooLong => "start_identity_too_long",
            Self::GoalTooLarge => "start_goal_too_large",
            Self::InvalidWorkspaceId => "start_workspace_id",
            Self::InvalidProviderRoute => "start_provider_route",
            Self::InvalidDeadline => "start_deadline",
            Self::InvalidEntropy => "start_entropy",
            Self::DuplicateBudget => "start_duplicate_budget",
            Self::TooManyTools => "start_too_many_tools",
            Self::InvalidToolName => "start_tool_name",
            Self::InvalidCreationRevision => "start_creation_revision",
            Self::InvalidConsentPreview => "start_consent_preview",
            Self::InvalidConsentReceipt => "start_consent_receipt",
            Self::InvalidBuiltinSkillBinding => "start_builtin_skill_binding",
            Self::ConsentBudgetMismatch => "start_consent_budget",
            Self::ProviderNotConfigured => "start_provider_not_configured",
            Self::InvalidReviewedWorkflow => "start_reviewed_workflow",
            Self::InvalidAgentWorkflow => "start_agent_workflow",
            Self::InvalidErrandWorkflow => "start_errand_workflow",
        }
    }
}

/// Whether this is one of the generated start shapes this decoder admits.
///
/// Selected-page research includes the one-page `BuildSourceTable` shape and
/// the bounded multi-page comparison and summary shapes. The reviewed local
/// workflow needs no model; `DirectUserKey` is the BYO path the agent table can
/// run, and `ManagedService` is the same agent shape over the product's own
/// worker.
///
/// An ordinary errand needs a model. An exact selected saved procedure uses
/// the same errand outcome but replays locally on one reviewed source. The
/// profile catalogue independently validates that version before this decoder.
pub(super) fn admits_generated_start(
    template_id: TaskTemplateId,
    command: &wire::StartTaskCommand,
    decoded_route: Option<&ProviderRouteId>,
) -> bool {
    let consent_route = command.consent_preview.provider_route;
    let route_admitted = match template_id {
        TaskTemplateId::BuildSourceTable => matches!(
            consent_route,
            wire::TaskProviderRoute::NoModelRequired
                | wire::TaskProviderRoute::DirectUserKey
                | wire::TaskProviderRoute::ManagedService
        ),
        TaskTemplateId::WebErrand => {
            selected_local_replay(command)
                || matches!(
                    consent_route,
                    wire::TaskProviderRoute::DirectUserKey
                        | wire::TaskProviderRoute::ManagedService
                )
        }
        TaskTemplateId::CompareProducts | TaskTemplateId::SummarizeEvidence => matches!(
            consent_route,
            wire::TaskProviderRoute::DirectUserKey | wire::TaskProviderRoute::ManagedService
        ),
    };
    route_admitted
        && command.provider_route_id.as_deref() == decoded_route.map(ProviderRouteId::as_str)
}

/// Identity presence selects the shape, never authority. `prepare_skill_start`
/// resolves it to an active, runnable, same-origin catalogue entry first.
fn selected_local_replay(command: &wire::StartTaskCommand) -> bool {
    command.template_id == wire::TaskTemplateId::WebErrand
        && command.consent_preview.provider_route == wire::TaskProviderRoute::NoModelRequired
        && command
            .skill_version_id
            .as_ref()
            .is_some_and(|id| !id.is_empty() && id.len() <= wire::MAX_IDENTIFIER_BYTES)
        && command.builtin_skill.is_none()
        && command.library_refresh.is_none()
}

pub(super) fn validate_start_workflow(
    command: &wire::StartTaskCommand,
    template_id: TaskTemplateId,
    tools: &[String],
    sources: &[ConsentedSource],
) -> Result<(), StartTaskDecodeError> {
    if matches!(template_id, TaskTemplateId::WebErrand) && !selected_local_replay(command) {
        return validate_errand_workflow(command, tools, sources);
    }
    if selected_local_replay(command)
        && (command.control_mode != wire::TaskControlMode::Assistant
            || command.kind != wire::TaskKind::Errand)
    {
        return Err(StartTaskDecodeError::InvalidReviewedWorkflow);
    }
    match command.consent_preview.provider_route {
        wire::TaskProviderRoute::NoModelRequired => validate_page_scoped_workflow(
            command,
            template_id,
            tools,
            sources,
            0,
            StartTaskDecodeError::InvalidReviewedWorkflow,
        ),
        wire::TaskProviderRoute::DirectUserKey => {
            if command.control_mode != wire::TaskControlMode::Assistant {
                return Err(StartTaskDecodeError::InvalidAgentWorkflow);
            }
            validate_page_scoped_workflow(
                command,
                template_id,
                tools,
                sources,
                DIRECT_USER_KEY_MAX_MODEL_REQUESTS,
                StartTaskDecodeError::InvalidAgentWorkflow,
            )
        }
        wire::TaskProviderRoute::ManagedService => {
            if command.control_mode != wire::TaskControlMode::Assistant {
                return Err(StartTaskDecodeError::InvalidAgentWorkflow);
            }
            validate_page_scoped_workflow(
                command,
                template_id,
                tools,
                sources,
                MANAGED_SERVICE_MAX_MODEL_REQUESTS,
                StartTaskDecodeError::InvalidAgentWorkflow,
            )
        }
        wire::TaskProviderRoute::NotConfigured => Err(StartTaskDecodeError::InvalidProviderRoute),
    }
}

/// The errand consent, allowlist and budget shape (decision 0087).
///
/// This is deliberately a separate function from
/// [`validate_page_scoped_workflow`] rather than a parameter on it. The two
/// shapes disagree about what a source is — a research task reads the page it
/// was given, an errand goes and finds one — so a shared validator would have
/// to take "may there be no source" and "may discovery be on" as arguments,
/// and a validator whose arguments can turn its own rules off is not one.
///
/// What an errand admits:
///
/// - **Zero or one named source.** Zero is the case the shape exists for: a
///   person says what they want done and Taffy finds where. One is a person
///   who happened to already be on the site.
/// - **Discovery on, with a cap the person consented to.** The cap bounds how
///   many origins may be added before the task must stop and ask again. It is
///   not authority: every origin is still granted one at a time, at proposal
///   time (decision 0033).
/// - **A source budget that agrees with the consent.** `MaxSources` must equal
///   what the person was shown — the sources they named plus the cap they
///   agreed to. A budget that disagreed with the consent surface would mean
///   the number the person read was not the number the task ran under.
/// - **A model route.** Enforced by [`admits_generated_start`]; the control
///   mode must be `Assistant`, because an errand nobody delegated is a
///   contradiction rather than a shape.
fn validate_errand_workflow(
    command: &wire::StartTaskCommand,
    tools: &[String],
    sources: &[ConsentedSource],
) -> Result<(), StartTaskDecodeError> {
    let error = StartTaskDecodeError::InvalidErrandWorkflow;
    if command.control_mode != wire::TaskControlMode::Assistant {
        return Err(error);
    }
    let max_model_requests = match command.consent_preview.provider_route {
        wire::TaskProviderRoute::DirectUserKey | wire::TaskProviderRoute::ManagedService => {
            ERRAND_MAX_MODEL_REQUESTS
        }
        wire::TaskProviderRoute::NoModelRequired | wire::TaskProviderRoute::NotConfigured => {
            return Err(error)
        }
    };
    let new_source_cap = command.consent_preview.new_source_cap;
    if sources.len() > 1
        || !command.consent_preview.source_discovery_enabled
        || new_source_cap == 0
        || new_source_cap > ERRAND_MAX_NEW_SOURCE_CAP
        || tools.is_empty()
        || !tools.iter().any(|tool| tool == REVIEWED_OBSERVATION_TOOL)
    {
        return Err(error);
    }
    let Ok(named) = u32::try_from(sources.len()) else {
        return Err(error);
    };
    let Some(max_sources) = named.checked_add(new_source_cap) else {
        return Err(error);
    };
    // An errand always has a model route — the match above refuses the two
    // that do not — so its retry budget is never the no-model zero.
    let required_budgets: [(wire::TaskBudgetKind, u64); 6] = [
        (wire::TaskBudgetKind::MaxSources, u64::from(max_sources)),
        (wire::TaskBudgetKind::MaxModelRequests, max_model_requests),
        (wire::TaskBudgetKind::MaxInputUnits, 0),
        (wire::TaskBudgetKind::MaxOutputUnits, 0),
        (wire::TaskBudgetKind::MaxCostUnits, 0),
        (
            wire::TaskBudgetKind::MaxRetriesPerStep,
            MAX_RETRIES_PER_STEP,
        ),
    ];
    if command.budgets.len() != required_budgets.len()
        || required_budgets.iter().any(|(kind, limit)| {
            !command
                .budgets
                .iter()
                .any(|budget| budget.kind == *kind && budget.limit == *limit)
        })
    {
        return Err(error);
    }
    Ok(())
}

/// Shared page-scoped consent, allowlist and budget shape.
///
/// The allowlist must contain [`REVIEWED_OBSERVATION_TOOL`], because that is
/// the tool this page-scoped start proposes and a task admitted without it is
/// journalled and then never advances; and it must not be empty, because
/// reducer guard evaluation reads an empty allowlist as everything the
/// milestone has reached (decision 0057). `max_model_requests` is the one
/// budget that differs: zero for the reviewed local workflow,
/// [`DIRECT_USER_KEY_MAX_MODEL_REQUESTS`] for BYO, and
/// [`MANAGED_SERVICE_MAX_MODEL_REQUESTS`] for the managed route.
fn validate_page_scoped_workflow(
    command: &wire::StartTaskCommand,
    template_id: TaskTemplateId,
    tools: &[String],
    sources: &[ConsentedSource],
    max_model_requests: u64,
    error: StartTaskDecodeError,
) -> Result<(), StartTaskDecodeError> {
    let source_count = u64::try_from(sources.len()).map_err(|_| error)?;
    let valid_source_count = sources.len() <= RESEARCH_MAX_SELECTED_SOURCES
        && match template_id {
            TaskTemplateId::BuildSourceTable => sources.len() == 1,
            TaskTemplateId::CompareProducts => sources.len() >= 2,
            TaskTemplateId::SummarizeEvidence => !sources.is_empty(),
            TaskTemplateId::WebErrand => selected_local_replay(command) && sources.len() == 1,
        };
    // A consent source has no historical snapshot field. One live tab cannot
    // name two current documents, even when a malformed caller gives those
    // rows different origins.
    let duplicate_tab = sources.iter().enumerate().any(|(index, source)| {
        sources.get(..index).is_some_and(|previous_sources| {
            previous_sources
                .iter()
                .any(|previous| previous.tab_id == source.tab_id)
        })
    });
    // A start that may call no model repeats no paid attempt, so the two
    // numbers move together: a retry budget over a zero request budget would
    // authorize an attempt the request budget already refuses.
    let max_retries_per_step = if max_model_requests == 0 {
        NO_MODEL_MAX_RETRIES_PER_STEP
    } else {
        MAX_RETRIES_PER_STEP
    };
    let required_budgets: [(wire::TaskBudgetKind, u64); 6] = [
        (wire::TaskBudgetKind::MaxSources, source_count),
        (wire::TaskBudgetKind::MaxModelRequests, max_model_requests),
        (wire::TaskBudgetKind::MaxInputUnits, 0),
        (wire::TaskBudgetKind::MaxOutputUnits, 0),
        (wire::TaskBudgetKind::MaxCostUnits, 0),
        (
            wire::TaskBudgetKind::MaxRetriesPerStep,
            max_retries_per_step,
        ),
    ];
    if !valid_source_count
        || duplicate_tab
        || command.consent_preview.source_discovery_enabled
        || command.consent_preview.new_source_cap != 0
        || tools.is_empty()
        || !tools.iter().any(|tool| tool == REVIEWED_OBSERVATION_TOOL)
        || command.budgets.len() != required_budgets.len()
        || required_budgets.iter().any(|(kind, limit)| {
            !command
                .budgets
                .iter()
                .any(|budget| budget.kind == *kind && budget.limit == *limit)
        })
    {
        return Err(error);
    }
    Ok(())
}
