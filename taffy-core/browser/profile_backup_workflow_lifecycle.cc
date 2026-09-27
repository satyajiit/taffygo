// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>

#include "taffy/browser/profile_backup_workflow.h"

namespace taffy {
namespace {

constexpr size_t kMaximumWindows = 8u;

}  // namespace

ProfileBackupWorkflow::WindowToken ProfileBackupWorkflow::RegisterWindow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (windows_.size() >= kMaximumWindows || next_window_token_ == 0u) {
    return 0u;
  }
  const WindowToken token = next_window_token_++;
  if (next_window_token_ == 0u || windows_.contains(token)) {
    next_window_token_ = 0u;
    return 0u;
  }
  windows_.emplace(token, WindowState{});
  return token;
}

bool ProfileBackupWorkflow::ActivateWindow(WindowToken window) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = windows_.find(window);
  if (found == windows_.end()) {
    return false;
  }
  if (std::ranges::any_of(windows_, [window](const auto& entry) {
        return entry.first != window && entry.second.active;
      })) {
    return false;
  }
  found->second.active = true;
  return true;
}

void ProfileBackupWorkflow::DeactivateWindow(WindowToken window) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (auto found = windows_.find(window); found != windows_.end()) {
    found->second.active = false;
  }
}

bool ProfileBackupWorkflow::IsActiveWindow(WindowToken window) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = windows_.find(window);
  return found != windows_.end() && found->second.active;
}

}  // namespace taffy
