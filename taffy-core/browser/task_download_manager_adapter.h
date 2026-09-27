// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_DOWNLOAD_MANAGER_ADAPTER_H_
#define TAFFY_BROWSER_TASK_DOWNLOAD_MANAGER_ADAPTER_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/functional/callback_forward.h"
#include "taffy/common/public/taffy_download_facts.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace taffy {

// The only task-facing projection of a Chromium DownloadItem. The GUID is an
// opaque manager identity; names, URLs, paths, referrers, subtypes, interrupt
// messages and page text have no field in this type.
struct TaskDownloadManagerSnapshot {
  std::string download_id;
  DownloadState state = DownloadState::kCreated;
  MediaTopLevelType media_type = MediaTopLevelType::kUnknown;
  uint64_t received_bytes = 0;
  DownloadDestinationKind directory_class = DownloadDestinationKind::kUndecided;
};

struct TaskDownloadManagerList {
  std::vector<TaskDownloadManagerSnapshot> downloads;
  bool truncated = false;
};

using TaskDownloadStartCallback =
    base::OnceCallback<void(std::optional<TaskDownloadManagerSnapshot>)>;
using TaskDownloadOpenCallback = base::OnceCallback<void(base::File)>;

// Starts one ordinary Chromium download from the exact live primary document.
// The callback is terminal even when the tab disappears or Chromium refuses
// before creating a DownloadItem.
void StartTaskDownload(content::WebContents* web_contents,
                       const GURL& destination,
                       TaskDownloadStartCallback callback);

// Returns at most `limit` newest manager items whose immutable original
// WebContents is exactly `web_contents`. The scan never falls back to all
// profile downloads when the tab is missing.
TaskDownloadManagerList ListTaskDownloads(content::WebContents* web_contents,
                                          size_t limit);

// Cancels the exact GUID only while it still belongs to `web_contents`.
// Already-cancelled is a verified idempotent success; complete items and
// unknown/cross-tab identities fail closed.
std::optional<TaskDownloadManagerSnapshot> CancelTaskDownload(
    content::WebContents* web_contents,
    const std::string& download_id);

// Opens the exact still-complete item described by a prior list snapshot on
// Chromium's download file sequence. The callback receives an already-open
// read descriptor or an invalid file; no path leaves this adapter.
void OpenCompletedTaskDownload(content::WebContents* web_contents,
                               const TaskDownloadManagerSnapshot& snapshot,
                               TaskDownloadOpenCallback callback);

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_DOWNLOAD_MANAGER_ADAPTER_H_
