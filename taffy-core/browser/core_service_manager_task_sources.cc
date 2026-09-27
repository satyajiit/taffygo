// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/check.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_navigation_authority.h"
#include "taffy/browser/task_source_selection_registry.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace taffy {

CoreTaskBrowserActions::CoreTaskBrowserActions(
    content::BrowserContext* browser_context)
    : task_browser_context_(browser_context),
      private_task_browser_profile_(browser_context &&
                                    browser_context->IsOffTheRecord()),
      task_source_selections_(
          std::make_unique<TaskSourceSelectionRegistry>(browser_context)) {
  CHECK(task_browser_context_);
}

CoreTaskBrowserActions::~CoreTaskBrowserActions() = default;

uint64_t CoreTaskBrowserActions::RegisterTaskSourceWindow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    return 0u;
  }
  const bool had_windows = task_source_selections_->HasRegisteredWindows();
  const uint64_t token = task_source_selections_->RegisterProductWindow();
  if (!had_windows && task_source_selections_->HasRegisteredWindows()) {
    OnTaskSourceWindowCountChanged();
  }
  return token;
}

void CoreTaskBrowserActions::UnregisterTaskSourceWindow(uint64_t window_token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool had_windows = task_source_selections_->HasRegisteredWindows();
  task_source_selections_->UnregisterProductWindow(window_token);
  if (had_windows != task_source_selections_->HasRegisteredWindows()) {
    OnTaskSourceWindowCountChanged();
  }
}

bool CoreTaskBrowserActions::RegisterTaskSourceTab(
    uint64_t window_token,
    int product_tab_id,
    content::WebContents* web_contents,
    bool restored_without_provenance) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsTaskBrowserAvailable() &&
         task_source_selections_->RegisterProductTab(
             window_token, product_tab_id, web_contents,
             restored_without_provenance);
}

bool CoreTaskBrowserActions::MarkAssistantCreatedTaskTab(
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || !web_contents ||
      web_contents->GetBrowserContext() != task_browser_context_) {
    return false;
  }
  TaskSourceSelectionRegistry::MarkAssistantCreatedTab(web_contents);
  return true;
}

void CoreTaskBrowserActions::UnregisterTaskSourceTab(uint64_t window_token,
                                                     int product_tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  task_source_selections_->UnregisterProductTab(window_token, product_tab_id);
}

bool CoreTaskBrowserActions::SelectTaskSourceTab(
    uint64_t window_token,
    int product_tab_id,
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsTaskBrowserAvailable() &&
         task_source_selections_->SelectProductTab(window_token, product_tab_id,
                                                   web_contents);
}

void CoreTaskBrowserActions::ClearTaskSourceSelection(uint64_t window_token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  task_source_selections_->ClearProductSelection(window_token);
}

bool CoreTaskBrowserActions::ActivateTaskSourceWindow(uint64_t window_token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool had_active_window = task_source_selections_->HasActiveWindow();
  const bool activated =
      IsTaskBrowserAvailable() &&
      task_source_selections_->ActivateProductWindow(window_token);
  if (activated && !had_active_window) {
    OnTaskSourceWindowActivated();
  }
  return activated;
}

void CoreTaskBrowserActions::DeactivateTaskSourceWindow(uint64_t window_token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  task_source_selections_->DeactivateProductWindow(window_token);
}

bool CoreTaskBrowserActions::BindTaskBrowserActionPlatform(
    uint64_t window_token,
    TaskBrowserActionPlatform* platform) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsTaskBrowserAvailable() && !private_task_browser_profile_ &&
         task_source_selections_->BindBrowserActionPlatform(window_token,
                                                            platform);
}

void CoreTaskBrowserActions::UnbindTaskBrowserActionPlatform(
    uint64_t window_token,
    TaskBrowserActionPlatform* platform) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  task_source_selections_->UnbindBrowserActionPlatform(window_token, platform);
}

bool CoreTaskBrowserActions::ClaimAssistantCreatedTaskTab(
    uint64_t window_token,
    const std::string& task_id,
    const std::string& action_id,
    content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsTaskBrowserAvailable() && !private_task_browser_profile_ &&
         task_source_selections_->ClaimAssistantCreatedTab(
             window_token, task_id, action_id, web_contents);
}

void CoreTaskBrowserActions::PrepareTaskDiscoveryTab(
    const std::string& task_id,
    const std::string& effect_id,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap,
    TaskDiscoveryPreparationCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsTaskBrowserAvailable() || private_task_browser_profile_) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  task_source_selections_->PrepareDiscoveryTab(
      task_id, effect_id, browser_session_id, remaining_new_source_cap,
      std::move(callback));
}

bool CoreTaskBrowserActions::IsExactTaskDiscoveryTab(
    const std::string& task_id,
    const std::string& tab_id,
    const std::string& browser_session_id,
    uint32_t remaining_new_source_cap,
    content::WebContents* web_contents) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return IsTaskBrowserAvailable() && !private_task_browser_profile_ &&
         task_source_selections_->IsExactDiscoveryTab(
             task_id, tab_id, browser_session_id, remaining_new_source_cap,
             web_contents);
}


IssuedSourceLiveness CoreTaskBrowserActions::IssuedTaskSourceLiveness(
    const core_service::mojom::TaskConsentSource& source) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return task_source_selections_->IssuedSourceLivenessOf(source);
}

bool CoreTaskBrowserActions::IsLiveIssuedTaskSource(
    const core_service::mojom::TaskConsentSource& source) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return task_source_selections_->IsLiveIssuedSource(source);
}

bool CoreTaskBrowserActions::HasRegisteredTaskSourceWindows() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return task_source_selections_->HasRegisteredWindows();
}

bool CoreTaskBrowserActions::HasActiveTaskSourceWindow() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return task_source_selections_->HasActiveWindow();
}

bool CoreServiceManager::IsTaskBrowserAvailable() const {
  return !shutdown_started_;
}

void CoreServiceManager::OnTaskSourceWindowCountChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RefreshIdleTeardown();
}

void CoreServiceManager::OnTaskSourceWindowActivated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DrainDeferredTaskDiscoveryBootstraps();
}

}  // namespace taffy
