// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_DOWNLOAD_OWNERSHIP_REGISTRY_H_
#define TAFFY_BROWSER_TASK_DOWNLOAD_OWNERSHIP_REGISTRY_H_

#include <stddef.h>

#include <deque>
#include <string>

#include "base/containers/flat_map.h"
#include "taffy/common/public/bip_identity.h"

namespace taffy {

// Bounded browser-process proof that an opaque Chromium download was created
// by one exact task. A list never writes this registry, so merely observing a
// manual or page-started download cannot turn it into assistant authority.
class TaskDownloadOwnershipRegistry {
 public:
  static constexpr size_t kMaxEntries = 128u;

  TaskDownloadOwnershipRegistry();
  TaskDownloadOwnershipRegistry(const TaskDownloadOwnershipRegistry&) = delete;
  TaskDownloadOwnershipRegistry& operator=(
      const TaskDownloadOwnershipRegistry&) = delete;
  ~TaskDownloadOwnershipRegistry();

  void Record(const TaskId& task_id,
              const std::string& browser_session_id,
              const TabId& tab_id,
              const std::string& download_id);
  bool Owns(const TaskId& task_id,
            const std::string& browser_session_id,
            const TabId& tab_id,
            const std::string& download_id) const;

  // Attribution for the person's completed-file controls. A completed file
  // outlives its source tab; this fact grants no assistant action authority.
  bool WasStartedBy(const TaskId& task_id,
                    const std::string& browser_session_id,
                    const std::string& download_id) const;

  size_t size_for_testing() const { return entries_.size(); }

 private:
  struct Owner {
    TaskId task_id;
    std::string browser_session_id;
    TabId tab_id;
  };

  base::flat_map<std::string, Owner> entries_;
  std::deque<std::string> insertion_order_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_DOWNLOAD_OWNERSHIP_REGISTRY_H_
