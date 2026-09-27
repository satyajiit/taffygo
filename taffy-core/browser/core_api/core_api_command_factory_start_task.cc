// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_api/core_api_start_task_shape.h"
#include "taffy/browser/core_api/task_consent_shape.h"
#include "taffy/browser/generated/product_capabilities.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

using start_task_shape::AreWellFormedResolvedSources;
using start_task_shape::HasExactResolvedHosts;
using start_task_shape::IsIdentifier;
using start_task_shape::IsOptionalIdentifier;
using start_task_shape::IsWellFormedToolAllowlist;
using start_task_shape::ProjectProviderRoute;
using start_task_shape::ProjectTemplate;
using start_task_shape::ProviderRouteId;

constexpr uint32_t kAssistantConfigVersion = 1;
constexpr uint32_t kPolicyVersion = 1;

bool AdmitsStartConsent(service::TaskTemplateId service_template,
                        const api::TaskConsentPreview& intent,
                        const service::TaskConsentPreview& resolved,
                        service::TaskProviderRoute route,
                        const std::optional<std::string>& skill_version_id) {
  const std::optional<std::string> route_id = ProviderRouteId(route);
  return resolved.provider_route == route &&
         intent.source_discovery_enabled ==
             resolved.source_discovery_enabled &&
         intent.new_source_cap == resolved.new_source_cap &&
         HasExactResolvedHosts(intent, resolved) &&
         IsAdmittedInitialTaskConsentShape(service_template, resolved,
                                           route_id, skill_version_id);
}

uint64_t MaxModelRequestsForStart(service::TaskTemplateId template_id,
                                  service::TaskProviderRoute route) {
  if (route == service::TaskProviderRoute::kNoModelRequired) {
    return 0u;
  }
  if (template_id == service::TaskTemplateId::kWebErrand) {
    return kWebErrandMaxModelRequests;
  }
  switch (route) {
    case service::TaskProviderRoute::kDirectUserKey:
      return kDirectUserKeyMaxModelRequests;
    case service::TaskProviderRoute::kManagedService:
      return kManagedServiceMaxModelRequests;
    case service::TaskProviderRoute::kNoModelRequired:
    case service::TaskProviderRoute::kNotConfigured:
      return 0u;
  }
  return 0u;
}

// How many paid attempts one model turn may add, for a start that may make
// `max_model_requests` of them (decision 0218).
//
// Derived rather than stated beside it, because the two are one decision: a
// retry budget over a zero request budget authorizes an attempt the request
// budget already refuses, and the core's decoder checks the pair. Stating the
// number twice is how they came apart the first time.
uint64_t MaxRetriesPerStepForStart(uint64_t max_model_requests) {
  return max_model_requests == 0u ? kNoModelMaxRetriesPerStep
                                  : kMaxRetriesPerStep;
}

service::TaskControlMode ControlModeForStart(
    service::TaskTemplateId template_id, service::TaskProviderRoute route) {
  if (template_id == service::TaskTemplateId::kWebErrand) {
    return service::TaskControlMode::kAssistant;
  }
  switch (route) {
    case service::TaskProviderRoute::kDirectUserKey:
    case service::TaskProviderRoute::kManagedService:
      return service::TaskControlMode::kAssistant;
    case service::TaskProviderRoute::kNoModelRequired:
    case service::TaskProviderRoute::kNotConfigured:
      return service::TaskControlMode::kShared;
  }
  return service::TaskControlMode::kShared;
}

}  // namespace

std::optional<ProjectedCoreCommand> CoreApiCommandFactory::BuildStartTask(
    std::string goal,
    api::TaskTemplateId template_id,
    std::optional<std::string> workspace_id,
    api::TaskConsentPreviewPtr consent_intent,
    service::TaskConsentPreviewPtr resolved_consent,
    std::vector<std::string> tool_allowlist,
    std::string browser_session_id,
    uint64_t service_generation,
    uint64_t now_monotonic_ms,
    std::optional<std::string> skill_offer_id,
    std::optional<std::string> skill_version_id) {
  const product_capabilities::Profile& capabilities =
      product_capabilities::Active();
  const std::optional<service::TaskTemplateId> service_template =
      ProjectTemplate(template_id);
  const std::optional<service::TaskProviderRoute> provider_route =
      consent_intent ? ProjectProviderRoute(consent_intent->provider_route)
                     : std::nullopt;
  // One named branch per clause. These used to be two compounded `if`s
  // answering one bare `std::nullopt`, which the facade turned into
  // `kInvalidRequest` and the sheet into "nothing was started" — seventeen
  // reasons with one spelling. The name is a compiled-in label and the log
  // carries nothing the person typed.
  const char* refusal = nullptr;
  if (!capabilities.delegated_task_start) {
    refusal = "capability/delegated-task-start";
  } else if (!capabilities.task_milestone) {
    refusal = "capability/task-milestone";
  } else if (browser_profile_id_.empty()) {
    refusal = "profile-id";
  } else if (goal.empty()) {
    refusal = "goal-empty";
  } else if (goal.size() > api::kMaxTaskGoalBytes) {
    refusal = "goal-too-long";
  } else if (!service_template) {
    refusal = "template";
  } else if (!IsOptionalIdentifier(workspace_id)) {
    refusal = "workspace-id";
  } else if (!consent_intent) {
    refusal = "consent-intent";
  } else if (!resolved_consent) {
    refusal = "resolved-consent";
  } else if (!provider_route) {
    refusal = "provider-route";
  } else if (!IsIdentifier(browser_session_id)) {
    refusal = "browser-session-id";
  } else if (!IsOptionalIdentifier(skill_offer_id)) {
    refusal = "skill-offer-id";
  } else if (!IsOptionalIdentifier(skill_version_id)) {
    refusal = "skill-version-id";
  } else if (skill_offer_id.has_value() != skill_version_id.has_value()) {
    refusal = "skill-offer-without-version";
  } else if (!IsWellFormedToolAllowlist(tool_allowlist)) {
    refusal = "tool-allowlist";
  } else if (!AreWellFormedResolvedSources(resolved_consent->sources)) {
    refusal = "resolved-sources";
  } else if (!AdmitsStartConsent(*service_template, *consent_intent,
                                 *resolved_consent, *provider_route,
                                 skill_version_id)) {
    refusal = "consent-shape";
  }
  if (refusal) {
    LOG(WARNING) << "[taffy_start_refused] at=build/" << refusal;
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kStartTask;
  core_command->start_task = api::StartTaskBody::New(
      entropy_source_->NewOpaqueId("request"), goal, template_id, workspace_id,
      consent_intent.Clone(), skill_offer_id);
  if (!HasValidGeneratedBody(*core_command)) {
    LOG(WARNING) << "[taffy_start_refused] at=build/generated-body";
    return std::nullopt;
  }

  auto start = service::StartTaskCommand::New();
  start->task_id = entropy_source_->NewOpaqueId("task");
  start->workspace_id = std::move(workspace_id);
  start->browser_profile_id = browser_profile_id_;
  // The kind follows the template: an errand ends on an outcome the browser
  // verified, and the reducer's finish rule and result shape read the kind
  // (decision 0136). Every research template stays research.
  start->kind = service_template == service::TaskTemplateId::kWebErrand
                    ? service::TaskKind::kErrand
                    : service::TaskKind::kResearch;
  start->goal = std::move(goal);
  start->control_mode = ControlModeForStart(*service_template, *provider_route);
  start->provider_route_id = ProviderRouteId(*provider_route);
  start->assistant_config_version = kAssistantConfigVersion;
  start->policy_version = kPolicyVersion;
  start->skill_version_id = std::move(skill_version_id);
  start->tool_allowlist = std::move(tool_allowlist);
  start->milestone = *capabilities.task_milestone;
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxSources,
      static_cast<uint64_t>(resolved_consent->sources.size()) +
          resolved_consent->new_source_cap));
  const uint64_t max_model_requests =
      MaxModelRequestsForStart(*service_template, *provider_route);
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxModelRequests, max_model_requests));
  start->budgets.push_back(
      service::TaskBudget::New(service::TaskBudgetKind::kMaxInputUnits, 0u));
  start->budgets.push_back(
      service::TaskBudget::New(service::TaskBudgetKind::kMaxOutputUnits, 0u));
  start->budgets.push_back(
      service::TaskBudget::New(service::TaskBudgetKind::kMaxCostUnits, 0u));
  start->budgets.push_back(
      service::TaskBudget::New(service::TaskBudgetKind::kMaxRetriesPerStep,
                               MaxRetriesPerStepForStart(max_model_requests)));
  start->has_task_deadline = false;
  start->task_deadline_monotonic_ms = 0;
  start->task_deadline_utc_ms = 0;
  start->predecessor_task_id = std::nullopt;
  start->trace_id = entropy_source_->NewOpaqueId("trace");
  const std::array<uint8_t, 32> seed = entropy_source_->NewTaskSeed();
  start->task_id_seed.assign(seed.begin(), seed.end());
  start->template_id = *service_template;
  start->consent_preview = std::move(resolved_consent);
  start->initial_consent_receipt_id =
      entropy_source_->NewOpaqueId("initial-consent");
  start->browser_session_id = std::move(browser_session_id);

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kStartTask;
  service_command->start_task = std::move(start);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

std::optional<ProjectedCoreCommand>
CoreApiCommandFactory::BuildStartLibraryRefresh(
    std::string preview_id,
    std::string collection_id,
    uint64_t expected_library_revision,
    uint64_t expected_workspace_revision,
    uint32_t source_count,
    std::string browser_session_id,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  const product_capabilities::Profile& capabilities =
      product_capabilities::Active();
  if (!capabilities.delegated_task_start || !capabilities.task_milestone) {
    return std::nullopt;
  }
  const bool preview_is_digest =
      preview_id.size() == api::kMaxLibraryRefreshPreviewIdBytes &&
      std::all_of(preview_id.begin(), preview_id.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
  if (browser_profile_id_.empty() || !preview_is_digest ||
      !IsIdentifier(collection_id) || expected_library_revision == 0u ||
      expected_workspace_revision == 0u || source_count == 0u ||
      source_count > service::kMaxLibraryRefreshSources ||
      !IsIdentifier(browser_session_id)) {
    return std::nullopt;
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kStartLibraryRefresh;
  core_command->start_library_refresh = api::StartLibraryRefreshBody::New(
      preview_id, collection_id, expected_library_revision,
      expected_workspace_revision, source_count);
  if (!HasValidGeneratedBody(*core_command)) {
    LOG(WARNING) << "[taffy_start_refused] at=build/refresh-generated-body";
    return std::nullopt;
  }

  auto start = service::StartTaskCommand::New();
  start->task_id = entropy_source_->NewOpaqueId("task");
  start->workspace_id = entropy_source_->NewOpaqueId("workspace");
  start->browser_profile_id = browser_profile_id_;
  start->kind = service::TaskKind::kResearch;
  start->goal = "Refresh saved sources";
  start->control_mode = service::TaskControlMode::kShared;
  start->provider_route_id = "no_model_required";
  start->assistant_config_version = kAssistantConfigVersion;
  start->policy_version = kPolicyVersion;
  start->skill_version_id = std::nullopt;
  start->tool_allowlist = {"browser.dom.read", "browser.navigate"};
  start->milestone = *capabilities.task_milestone;
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxSources, source_count));
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxModelRequests, 0u));
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxInputUnits, 0u));
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxOutputUnits, 0u));
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxCostUnits, 0u));
  start->budgets.push_back(service::TaskBudget::New(
      service::TaskBudgetKind::kMaxRetriesPerStep, kNoModelMaxRetriesPerStep));
  start->has_task_deadline = false;
  start->task_deadline_monotonic_ms = 0u;
  start->task_deadline_utc_ms = 0u;
  start->predecessor_task_id = std::nullopt;
  start->trace_id = entropy_source_->NewOpaqueId("trace");
  const std::array<uint8_t, 32> seed = entropy_source_->NewTaskSeed();
  start->task_id_seed.assign(seed.begin(), seed.end());
  start->template_id = service::TaskTemplateId::kWebErrand;
  start->consent_preview = service::TaskConsentPreview::New(
      std::vector<service::TaskConsentSourcePtr>(), true, source_count,
      service::TaskProviderRoute::kNoModelRequired);
  start->initial_consent_receipt_id =
      entropy_source_->NewOpaqueId("initial-consent");
  start->browser_session_id = std::move(browser_session_id);
  start->library_refresh = service::LibraryRefreshRequest::New(
      std::move(preview_id), expected_library_revision,
      std::move(collection_id), expected_workspace_revision,
      std::vector<service::LibraryRefreshSourcePtr>());

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kStartTask;
  service_command->start_task = std::move(start);
  return ProjectedCoreCommand{std::move(core_command),
                              std::move(service_command)};
}

}  // namespace taffy
