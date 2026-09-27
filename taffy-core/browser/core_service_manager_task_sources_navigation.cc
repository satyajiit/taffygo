// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_navigation_authority.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

// The browser moves a task's own tabs make — a search from the discovery tab,
// a link the model observed, a navigate, a tab control — resolved against the
// registry's live authority. Split from the registration and selection
// bookkeeping the same class owns.

namespace taffy {

bool CoreServiceManager::IsTaskSourceTabForDisplay(
    const std::string& task_id,
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto revision = FindTaskRevision(task_id);
  if (!revision || availability_ != Availability::kReady ||
      !IsTaskBrowserAvailable() || !web_contents ||
      web_contents->GetBrowserContext() != browser_context_ ||
      browser_context_->IsOffTheRecord()) {
    return false;
  }
  auto* host = TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const auto live = host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live) {
    return false;
  }
  const auto source = accepted_approvals_.FindTaskSourceForDisplay(
      task_id, live->tab_id, *revision, service_generation_);
  return source && source->normalized_origin == live->origin &&
         IsLiveIssuedTaskSource(*source);
}

std::optional<std::string> CoreTaskBrowserActions::ResolveTaskSearchAddress(
    const std::string& tab_id,
    const std::string& query) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_ ||
      tab_id.empty() || query.empty()) {
    return std::nullopt;
  }
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(task_browser_context_.get(), tab_id);
  content::WebContents* web_contents =
      host ? host->observed_web_contents() : nullptr;
  TaskBrowserActionPlatform* platform =
      task_source_selections_->BrowserActionPlatformFor(web_contents);
  return platform ? platform->ResolveSearchAddress(query) : std::nullopt;
}

std::optional<core_service::mojom::TaskConsentSourcePtr>
CoreTaskBrowserActions::IssueDiscoveredTaskSourceForAction(
    const std::string& task_id,
    const std::string& action_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return std::nullopt;
  }
  return task_source_selections_->IssueDiscoveredSourceForAction(task_id,
                                                                 action_id);
}

std::optional<core_service::mojom::TaskConsentSourcePtr>
CoreTaskBrowserActions::IssueDiscoveredTaskSourceForTab(
    const std::string& task_id,
    const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return std::nullopt;
  }
  return task_source_selections_->IssueDiscoveredSourceForTab(task_id, tab_id);
}

BrowserActionStart CoreTaskBrowserActions::OpenTaskTab(
    const TaskId& task_id,
    const ActionId& action_id,
    const TabId& opener_tab_id,
    content::WebContents* opener_web_contents,
    const std::string& normalized_destination) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  BrowserActionStart result;
  TaffyPageIntelligenceHost* host = FindPageIntelligenceHost(
      task_browser_context_.get(), opener_tab_id.value);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_ || !host ||
      host->observed_web_contents() != opener_web_contents) {
    return result;
  }
  TaskBrowserActionPlatform* platform =
      task_source_selections_->BrowserActionPlatformFor(opener_web_contents);
  if (!platform) {
    return result;
  }
  content::WebContents* created = platform->OpenTaskTab(
      task_id.value, action_id.value, normalized_destination);
  if (!created || created->GetBrowserContext() != task_browser_context_ ||
      !task_source_selections_->IsTaskOwnedTab(task_id.value, created)) {
    if (created && task_source_selections_->HasExactTaskTabClaim(
                       task_id.value, action_id.value, created)) {
      platform->CloseTaskTab(created);
    }
    task_source_selections_->ForgetUnpublishedTaskTab(task_id.value, created);
    return result;
  }
  result.started = true;
  result.created_web_contents = created;
  return result;
}

bool CoreTaskBrowserActions::StartBrowserSearch(
    const TaskId& task_id,
    const TabId& tab_id,
    content::WebContents* web_contents,
    const std::string& query,
    const std::string& normalized_destination,
    const std::optional<TaskDiscoveryCapabilityBinding>& discovery) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_ ||
      !task_id.is_valid() || query.empty() || normalized_destination.empty()) {
    return false;
  }
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(task_browser_context_.get(), tab_id.value);
  if (!host || host->observed_web_contents() != web_contents) {
    return false;
  }
  if (discovery) {
    const std::optional<TaskDiscoveryDocumentContext> live =
        ResolveTaskDiscoveryDocument(task_browser_context_.get(), tab_id.value);
    if (!live || discovery->tab_id != tab_id ||
        discovery->frame_id.value != live->frame_id ||
        discovery->page_epoch.value != live->page_epoch ||
        discovery->opaque_origin_id != live->opaque_origin_id ||
        !IsExactTaskDiscoveryTab(
            task_id.value, tab_id.value, discovery->browser_session_id,
            discovery->remaining_new_source_cap, web_contents)) {
      return false;
    }
  } else if (!ResolveTaskPolicyDocument(task_browser_context_.get(),
                                        tab_id.value)) {
    // Ordinary search always starts from an exact tuple document. An opaque
    // tab is usable only through the discovery capability branch above.
    return false;
  }
  TaskBrowserActionPlatform* platform =
      task_source_selections_->BrowserActionPlatformFor(web_contents);
  if (!platform) {
    return false;
  }
  const std::optional<std::string> resolved =
      platform->ResolveSearchAddress(query);
  return resolved && *resolved == normalized_destination &&
         platform->StartSearch(web_contents, query, normalized_destination);
}

bool CoreTaskBrowserActions::StartObservedLinkNavigation(
    const TabId& tab_id,
    content::WebContents* web_contents,
    const NodeHandle& source_handle,
    const std::string& normalized_destination) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_ ||
      !web_contents || normalized_destination.empty() ||
      source_handle.tab_id != tab_id) {
    return false;
  }
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(task_browser_context_.get(), tab_id.value);
  const std::optional<std::string> resolved =
      host ? host->ResolveObservedLink(CanonicalLinkOpenHandle{
                 .tab_id = source_handle.tab_id.value,
                 .frame_id = source_handle.frame_id.value,
                 .page_epoch = source_handle.page_epoch.value,
                 .graph_revision = source_handle.graph_revision,
                 .node_id = source_handle.node_id.value,
                 .expected_origin_is_opaque =
                     source_handle.expected_origin.is_opaque(),
                 .expected_origin = source_handle.expected_origin.serialization,
             })
           : std::nullopt;
  const GURL destination(normalized_destination);
  const char* refused = nullptr;
  if (!host) {
    refused = "no-host";
  } else if (host->observed_web_contents() != web_contents) {
    refused = "other-web-contents";
  } else if (!resolved) {
    refused = "not-in-registry";
  } else if (*resolved != normalized_destination) {
    refused = "destination-differs";
  } else if (!destination.is_valid() || !destination.SchemeIsHTTPOrHTTPS() ||
             destination.has_username() || destination.has_password() ||
             destination.spec() != normalized_destination) {
    refused = "address-shape";
  }
  if (refused) {
    LOG(WARNING) << "[taffy_link_open_start_refused] at=" << refused;
    return false;
  }

  content::NavigationController::LoadURLParams params(destination);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  params.is_renderer_initiated = false;
  params.has_user_gesture = false;
  params.navigation_ui_data =
      TaskNavigationAuthority::CreateNavigationData(destination);
  if (!params.navigation_ui_data) {
    return false;
  }
  const bool started =
      !!web_contents->GetController().LoadURLWithParams(params);
  LOG(WARNING) << "[taffy_link_open_started] started=" << (started ? 1 : 0);
  return started;
}

bool CoreTaskBrowserActions::StartTabControl(
    BrowserCommandType command_type,
    const TabId& tab_id,
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_ ||
      !web_contents) {
    return false;
  }
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(task_browser_context_.get(), tab_id.value);
  NavigationLifecycleTracker* tracker =
      NavigationLifecycleTracker::FromWebContents(web_contents);
  return host && host->observed_web_contents() == web_contents && tracker &&
         tracker->ExecuteControl(command_type);
}

bool CoreTaskBrowserActions::ReleaseOwnedTaskTabs(const std::string& task_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!task_source_selections_->CanReconcileTaskTabs(task_id)) {
    return false;
  }
  for (content::WebContents* web_contents :
       task_source_selections_->BeginReleaseTaskTabs(task_id)) {
    TaskBrowserActionPlatform* platform =
        task_source_selections_->BrowserActionPlatformFor(web_contents);
    const bool closed = platform && platform->CloseTaskTab(web_contents);
    task_source_selections_->CompleteTaskTabRelease(web_contents, closed);
  }
  return !task_source_selections_->HasTaskTabsPendingRelease(task_id);
}

std::optional<core_service::mojom::TaskTabActionResultPtr>
CoreTaskBrowserActions::ListOwnedTaskTabs(
    const std::string& task_id,
    const std::string& browser_session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return std::nullopt;
  }
  return task_source_selections_->ListOwnedTaskTabs(task_id,
                                                    browser_session_id);
}

std::optional<core_service::mojom::TaskTabActionResultPtr>
CoreTaskBrowserActions::ActivateOwnedTaskTab(
    const std::string& task_id,
    const std::string& browser_session_id,
    const core_service::mojom::TaskTabDocumentTarget& target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return std::nullopt;
  }
  return task_source_selections_->ActivateOwnedTaskTab(
      task_id, browser_session_id, target);
}

std::optional<core_service::mojom::TaskTabActionResultPtr>
CoreTaskBrowserActions::CloseOwnedTaskTab(
    const std::string& task_id,
    const std::string& browser_session_id,
    const core_service::mojom::TaskTabDocumentTarget& target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return std::nullopt;
  }
  return task_source_selections_->CloseOwnedTaskTab(task_id, browser_session_id,
                                                    target);
}

std::optional<core_service::mojom::TaskConsentPreviewPtr>
CoreTaskBrowserActions::ResolveStartTaskConsent(
    core_api::mojom::TaskTemplateId template_id,
    const core_api::mojom::TaskConsentPreview& selection_intent,
    const std::optional<std::string>& skill_offer_id,
    core_api::mojom::CoreApiSubmissionStatus* verdict) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The registry below names each of its own clauses. This one stands above
  // it and answered nothing at all, so a start refused here was
  // indistinguishable from a start the registry refused. A browser shutting
  // down or a private profile is somewhere Taffy cannot work, which is not
  // something a person can fix by rewording (decision 0231).
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    LOG(WARNING) << "[taffy_start_refused] at=consent/task-browser"
                 << " available=" << IsTaskBrowserAvailable()
                 << " private=" << private_task_browser_profile_;
    if (verdict) {
      *verdict = core_api::mojom::CoreApiSubmissionStatus::kCoreUnavailable;
    }
    return std::nullopt;
  }
  return task_source_selections_->ResolveConsentPreview(
      template_id, selection_intent, skill_offer_id, verdict);
}

}  // namespace taffy
