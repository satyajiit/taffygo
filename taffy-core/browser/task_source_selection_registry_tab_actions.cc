// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>

#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

bool IsTaskIdentity(const std::string& value) {
  return !value.empty() && value.size() <= service::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool SameTarget(const service::TaskTabDocumentTarget& left,
                const service::TaskTabDocumentTarget& right) {
  return left.tab_id == right.tab_id && left.frame_id == right.frame_id &&
         left.page_epoch == right.page_epoch &&
         left.graph_revision == right.graph_revision;
}

}  // namespace

service::TaskTabDocumentTargetPtr
TaskSourceSelectionRegistry::LiveOwnedTaskTabTarget(
    const OwnedTaskTab& owned) const {
  content::WebContents* web_contents = owned.web_contents.get();
  if (!web_contents || !owned.product_tab_id || owned.window_token == 0u ||
      !IsRegisteredProductContents(web_contents) ||
      BrowserOwnedProvenance(web_contents) !=
          TaskSourceTabProvenance::kAssistantCreated) {
    return nullptr;
  }
  const auto window = windows_.find(owned.window_token);
  if (window == windows_.end()) {
    return nullptr;
  }
  const auto product_tab = window->second.tabs.find(*owned.product_tab_id);
  if (product_tab == window->second.tabs.end() ||
      product_tab->second.web_contents.get() != web_contents ||
      product_tab->second.provenance !=
          TaskSourceTabProvenance::kAssistantCreated) {
    return nullptr;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  if (!live || live->graph_revision == 0u) {
    return nullptr;
  }
  return service::TaskTabDocumentTarget::New(
      live->tab_id, live->frame_id, live->page_epoch, live->graph_revision);
}

TaskSourceSelectionRegistry::OwnedTaskTab*
TaskSourceSelectionRegistry::FindExactOwnedTaskTab(
    const std::string& task_id,
    const service::TaskTabDocumentTarget& target) {
  for (OwnedTaskTab& owned : owned_task_tabs_) {
    if (owned.task_id != task_id) {
      continue;
    }
    service::TaskTabDocumentTargetPtr live = LiveOwnedTaskTabTarget(owned);
    if (live && SameTarget(*live, target)) {
      return &owned;
    }
  }
  return nullptr;
}

bool TaskSourceSelectionRegistry::IsOwnedTaskTabActive(
    const OwnedTaskTab& owned) const {
  if (!owned.product_tab_id || owned.window_token == 0u ||
      !active_windows_.contains(owned.window_token)) {
    return false;
  }
  const auto window = windows_.find(owned.window_token);
  return window != windows_.end() &&
         window->second.selected_product_tab_id == owned.product_tab_id;
}

std::optional<service::TaskTabActionResultPtr>
TaskSourceSelectionRegistry::ListOwnedTaskTabs(
    const std::string& task_id,
    const std::string& browser_session_id) {
  if (!IsTaskIdentity(task_id) || !IsTaskIdentity(browser_session_id)) {
    return std::nullopt;
  }
  PruneOwnedTaskTabs();
  auto result = service::TaskTabActionResult::New();
  result->browser_session_id = browser_session_id;
  result->operation_kind = service::TaskActionOperationKind::kTabsList;
  result->postcondition = service::TaskTabPostcondition::kListed;
  for (const OwnedTaskTab& owned : owned_task_tabs_) {
    if (owned.task_id != task_id) {
      continue;
    }
    service::TaskTabDocumentTargetPtr target = LiveOwnedTaskTabTarget(owned);
    if (!target) {
      continue;
    }
    if (result->tabs.size() >= service::kMaxTaskTabResults) {
      return std::nullopt;
    }
    result->tabs.push_back(service::TaskTabSnapshot::New(
        std::move(target), IsOwnedTaskTabActive(owned)));
  }
  result->state_was_already_satisfied = false;
  return std::optional<service::TaskTabActionResultPtr>(std::move(result));
}

std::optional<service::TaskTabActionResultPtr>
TaskSourceSelectionRegistry::ActivateOwnedTaskTab(
    const std::string& task_id,
    const std::string& browser_session_id,
    const service::TaskTabDocumentTarget& target) {
  if (!IsTaskIdentity(task_id) || !IsTaskIdentity(browser_session_id)) {
    return std::nullopt;
  }
  PruneOwnedTaskTabs();
  OwnedTaskTab* owned = FindExactOwnedTaskTab(task_id, target);
  if (!owned || !active_windows_.contains(owned->window_token)) {
    return std::nullopt;
  }
  const bool already_active = IsOwnedTaskTabActive(*owned);
  if (!already_active) {
    content::WebContents* web_contents = owned->web_contents.get();
    TaskBrowserActionPlatform* platform =
        BrowserActionPlatformFor(web_contents);
    if (!platform || !platform->ActivateTaskTab(web_contents)) {
      return std::nullopt;
    }
    owned = FindExactOwnedTaskTab(task_id, target);
    if (!owned || !IsOwnedTaskTabActive(*owned)) {
      return std::nullopt;
    }
  }
  auto result = service::TaskTabActionResult::New();
  result->browser_session_id = browser_session_id;
  result->operation_kind = service::TaskActionOperationKind::kTabsActivate;
  result->postcondition = service::TaskTabPostcondition::kActive;
  result->target = target.Clone();
  result->state_was_already_satisfied = already_active;
  return std::optional<service::TaskTabActionResultPtr>(std::move(result));
}

std::optional<service::TaskTabActionResultPtr>
TaskSourceSelectionRegistry::CloseOwnedTaskTab(
    const std::string& task_id,
    const std::string& browser_session_id,
    const service::TaskTabDocumentTarget& target) {
  if (!IsTaskIdentity(task_id) || !IsTaskIdentity(browser_session_id)) {
    return std::nullopt;
  }
  PruneOwnedTaskTabs();
  OwnedTaskTab* owned = FindExactOwnedTaskTab(task_id, target);
  if (!owned) {
    const bool closed = std::any_of(
        closed_task_tabs_.begin(), closed_task_tabs_.end(),
        [&](const ClosedTaskTab& candidate) {
          return candidate.task_id == task_id &&
                 candidate.browser_session_id == browser_session_id &&
                 candidate.tab_id == target.tab_id &&
                 candidate.frame_id == target.frame_id &&
                 candidate.page_epoch == target.page_epoch &&
                 candidate.graph_revision == target.graph_revision;
        });
    if (!closed) {
      return std::nullopt;
    }
    auto result = service::TaskTabActionResult::New();
    result->browser_session_id = browser_session_id;
    result->operation_kind = service::TaskActionOperationKind::kTabsClose;
    result->postcondition = service::TaskTabPostcondition::kAbsent;
    result->target = target.Clone();
    result->state_was_already_satisfied = true;
    return std::optional<service::TaskTabActionResultPtr>(std::move(result));
  }

  content::WebContents* web_contents = owned->web_contents.get();
  const std::string closed_action_id = owned->action_id;
  TaskBrowserActionPlatform* platform = BrowserActionPlatformFor(web_contents);
  if (!platform || !platform->CloseTaskTab(web_contents) ||
      FindExactOwnedTaskTab(task_id, target)) {
    return std::nullopt;
  }
  std::erase_if(owned_task_tabs_, [&](const OwnedTaskTab& candidate) {
    return candidate.task_id == task_id &&
           candidate.action_id == closed_action_id;
  });
  if (closed_task_tabs_.size() >= closed_task_tab_limit_) {
    closed_task_tabs_.pop_front();
  }
  closed_task_tabs_.push_back(ClosedTaskTab{
      .task_id = task_id,
      .browser_session_id = browser_session_id,
      .tab_id = target.tab_id,
      .frame_id = target.frame_id,
      .page_epoch = target.page_epoch,
      .graph_revision = target.graph_revision,
  });
  auto result = service::TaskTabActionResult::New();
  result->browser_session_id = browser_session_id;
  result->operation_kind = service::TaskActionOperationKind::kTabsClose;
  result->postcondition = service::TaskTabPostcondition::kAbsent;
  result->target = target.Clone();
  result->state_was_already_satisfied = false;
  return std::optional<service::TaskTabActionResultPtr>(std::move(result));
}

}  // namespace taffy
