// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_download_ownership_registry.h"

#include <utility>

namespace taffy {

TaskDownloadOwnershipRegistry::TaskDownloadOwnershipRegistry() = default;
TaskDownloadOwnershipRegistry::~TaskDownloadOwnershipRegistry() = default;

void TaskDownloadOwnershipRegistry::Record(
    const TaskId& task_id,
    const std::string& browser_session_id,
    const TabId& tab_id,
    const std::string& download_id) {
  if (!task_id.is_valid() || browser_session_id.empty() || !tab_id.is_valid() ||
      download_id.empty()) {
    return;
  }
  auto existing = entries_.find(download_id);
  if (existing != entries_.end()) {
    // Chromium GUIDs are immutable. A duplicate callback may confirm the same
    // owner, but it must never transfer authority to a different task tuple.
    return;
  }
  while (entries_.size() >= kMaxEntries && !insertion_order_.empty()) {
    entries_.erase(insertion_order_.front());
    insertion_order_.pop_front();
  }
  entries_.emplace(download_id, Owner{task_id, browser_session_id, tab_id});
  insertion_order_.push_back(download_id);
}

bool TaskDownloadOwnershipRegistry::Owns(
    const TaskId& task_id,
    const std::string& browser_session_id,
    const TabId& tab_id,
    const std::string& download_id) const {
  const auto found = entries_.find(download_id);
  return found != entries_.end() && found->second.task_id == task_id &&
         found->second.browser_session_id == browser_session_id &&
         found->second.tab_id == tab_id;
}

bool TaskDownloadOwnershipRegistry::WasStartedBy(
    const TaskId& task_id,
    const std::string& browser_session_id,
    const std::string& download_id) const {
  const auto found = entries_.find(download_id);
  return found != entries_.end() && found->second.task_id == task_id &&
         found->second.browser_session_id == browser_session_id;
}

}  // namespace taffy
