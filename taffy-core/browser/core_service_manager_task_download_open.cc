// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager.h"

#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"

namespace taffy {

bool CoreServiceManager::CanOpenTaskDownloadForPerson(
    const std::string& task_id,
    const std::string& download_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto terminal = FindTerminalTask(task_id);
  const auto* published = state_cache_.latest();
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !browser_context_ || browser_context_->IsOffTheRecord() || !terminal ||
      !published || published->service_generation != service_generation_ ||
      published->sequence != state_bindings_.state_sequence() ||
      terminal->kind == core_service::mojom::TerminalTaskKind::kCancelled ||
      !task_download_ownership_.WasStartedBy(
          TaskId{task_id}, browser_session_id_, download_id)) {
    return false;
  }
  auto* downloads = browser_context_->GetDownloadManager();
  auto* item = downloads ? downloads->GetDownloadByGuid(download_id) : nullptr;
  if (!item || item->GetGuid() != download_id ||
      item->GetState() != download::DownloadItem::COMPLETE ||
      item->GetFileExternallyRemoved() || item->IsDangerous() ||
      !item->CanOpenDownload()) {
    return false;
  }
  const auto insecure = item->GetInsecureDownloadStatus();
  return insecure == download::DownloadItem::InsecureDownloadStatus::SAFE ||
         insecure == download::DownloadItem::InsecureDownloadStatus::VALIDATED;
}

}  // namespace taffy
