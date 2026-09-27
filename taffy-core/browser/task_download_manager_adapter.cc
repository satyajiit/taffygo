// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_download_manager_adapter.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/files/file.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_source.h"
#include "components/download/public/common/download_url_parameters.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "taffy/browser/taffy_browser_effect_source.h"
#include "url/gurl.h"

namespace taffy {
namespace {

constexpr net::NetworkTrafficAnnotationTag kTaskDownloadTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("taffy_task_download", R"(
      semantics {
        sender: "Taffy assistant download"
        description:
          "Downloads the exact HTTPS address a person delegated to the "
          "built-in Taffy assistant from the exact live browser document. "
          "The request remains associated with that frame and passes through "
          "Chromium's ordinary download safety and destination handling."
        trigger:
          "A person authorizes a browser.download.start task action."
        data:
          "The delegated HTTPS address and ordinary request metadata needed "
          "by Chromium's download stack. No page text, file name, local path "
          "or model response is added by TaffyGo."
        destination: WEBSITE
        internal { contacts { owners: "//taffy/OWNERS" } }
        user_data { type: WEB_CONTENT }
        last_reviewed: "2026-08-30"
      }
      policy {
        cookies_allowed: YES
        cookies_store: "user"
        setting:
          "The person can cancel or remove the transfer with the browser's "
          "ordinary Downloads controls."
        policy_exception_justification: "Not implemented."
      })");

DownloadState DownloadStateOf(const download::DownloadItem& item) {
  switch (item.GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      return item.IsPaused() ? DownloadState::kPaused
                             : DownloadState::kInProgress;
    case download::DownloadItem::COMPLETE:
      return DownloadState::kComplete;
    case download::DownloadItem::CANCELLED:
      return DownloadState::kCancelled;
    case download::DownloadItem::INTERRUPTED:
      return DownloadState::kInterrupted;
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      return DownloadState::kCreated;
  }
}

TaskDownloadManagerSnapshot Project(const download::DownloadItem& item) {
  const int64_t received = item.GetReceivedBytes();
  return {
      .download_id = item.GetGuid(),
      .state = DownloadStateOf(item),
      .media_type = MediaTopLevelTypeOf(item.GetMimeType()),
      .received_bytes = received < 0 ? 0u : static_cast<uint64_t>(received),
      // The adapter never reads a local path. The separately installed
      // browser witness may classify registered directory roots, but the task
      // result stays undecided rather than acquiring filesystem content.
      .directory_class = DownloadDestinationKind::kUndecided,
  };
}

void OnTaskDownloadStarted(base::WeakPtr<content::WebContents> web_contents,
                           TaskDownloadStartCallback callback,
                           download::DownloadItem* item,
                           download::DownloadInterruptReason reason) {
  if (!web_contents || !item ||
      reason != download::DOWNLOAD_INTERRUPT_REASON_NONE ||
      content::DownloadItemUtils::GetOriginalWebContents(item) !=
          web_contents.get()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  TaskDownloadManagerSnapshot snapshot = Project(*item);
  if (snapshot.download_id.empty()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::move(callback).Run(std::move(snapshot));
}

bool IsNewer(const download::DownloadItem* left,
             const download::DownloadItem* right) {
  if (left->GetStartTime() != right->GetStartTime()) {
    return left->GetStartTime() > right->GetStartTime();
  }
  return left->GetGuid() < right->GetGuid();
}

base::File OpenExactDownloadFile(base::FilePath path, uint64_t expected_bytes) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return base::File();
  }
  const int64_t length = file.GetLength();
  if (length < 0 || static_cast<uint64_t>(length) != expected_bytes) {
    return base::File();
  }
  return file;
}

}  // namespace

void StartTaskDownload(content::WebContents* web_contents,
                       const GURL& destination,
                       TaskDownloadStartCallback callback) {
  if (!callback) {
    return;
  }
  content::RenderFrameHost* frame =
      web_contents ? web_contents->GetPrimaryMainFrame() : nullptr;
  content::DownloadManager* manager =
      web_contents ? web_contents->GetBrowserContext()->GetDownloadManager()
                   : nullptr;
  if (!frame || !frame->IsRenderFrameLive() || !manager ||
      !destination.is_valid() || !destination.SchemeIs("https") ||
      destination.has_username() || destination.has_password()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  auto parameters = frame->CreateDownloadUrlParameters(
      destination, kTaskDownloadTrafficAnnotation);
  if (!parameters) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  parameters->set_content_initiated(false);
  parameters->set_download_source(download::DownloadSource::WEB_CONTENTS_API);
  parameters->set_require_safety_checks(true);
  parameters->set_callback(base::BindOnce(
      &OnTaskDownloadStarted, web_contents->GetWeakPtr(), std::move(callback)));
  manager->DownloadUrl(std::move(parameters));
}

TaskDownloadManagerList ListTaskDownloads(content::WebContents* web_contents,
                                          size_t limit) {
  TaskDownloadManagerList result;
  if (!web_contents || limit == 0u) {
    return result;
  }
  content::DownloadManager* manager =
      web_contents->GetBrowserContext()->GetDownloadManager();
  if (!manager) {
    return result;
  }

  download::SimpleDownloadManager::DownloadVector all_downloads;
  manager->GetAllDownloads(&all_downloads);
  manager->GetUninitializedActiveDownloadsIfAny(&all_downloads);

  // Keep only the newest `limit + 1` pointers. This provides a truncation
  // witness without sorting or projecting every item in a potentially long
  // profile history.
  std::vector<download::DownloadItem*> bounded;
  bounded.reserve(limit + 1u);
  size_t exact_tab_count = 0u;
  for (download::DownloadItem* item : all_downloads) {
    if (!item ||
        content::DownloadItemUtils::GetOriginalWebContents(item) !=
            web_contents ||
        item->GetGuid().empty()) {
      continue;
    }
    ++exact_tab_count;
    const auto position =
        std::lower_bound(bounded.begin(), bounded.end(), item, IsNewer);
    if (position != bounded.end() || bounded.size() <= limit) {
      bounded.insert(position, item);
      if (bounded.size() > limit + 1u) {
        bounded.pop_back();
      }
    }
  }

  result.truncated = exact_tab_count > limit;
  const size_t projected_count = std::min(limit, bounded.size());
  result.downloads.reserve(projected_count);
  for (size_t index = 0; index < projected_count; ++index) {
    result.downloads.push_back(Project(*bounded[index]));
  }
  return result;
}

std::optional<TaskDownloadManagerSnapshot> CancelTaskDownload(
    content::WebContents* web_contents,
    const std::string& download_id) {
  content::DownloadManager* manager =
      web_contents ? web_contents->GetBrowserContext()->GetDownloadManager()
                   : nullptr;
  download::DownloadItem* item =
      manager && !download_id.empty()
          ? manager->GetDownloadByGuid(download_id)
          : nullptr;
  if (!item || content::DownloadItemUtils::GetOriginalWebContents(item) !=
                   web_contents) {
    return std::nullopt;
  }
  TaskDownloadManagerSnapshot current = Project(*item);
  if (current.download_id != download_id) {
    return std::nullopt;
  }
  if (current.state == DownloadState::kCancelled) {
    return current;
  }
  if (current.state == DownloadState::kComplete) {
    return std::nullopt;
  }
  item->Cancel(/*user_cancel=*/true);
  current = Project(*item);
  return current.state == DownloadState::kCancelled
             ? std::make_optional(std::move(current))
             : std::nullopt;
}

void OpenCompletedTaskDownload(content::WebContents* web_contents,
                               const TaskDownloadManagerSnapshot& snapshot,
                               TaskDownloadOpenCallback callback) {
  if (!callback) {
    return;
  }
  content::DownloadManager* manager =
      web_contents ? web_contents->GetBrowserContext()->GetDownloadManager()
                   : nullptr;
  download::DownloadItem* item =
      manager ? manager->GetDownloadByGuid(snapshot.download_id) : nullptr;
  const bool safe_insecure_status =
      item && (item->GetInsecureDownloadStatus() ==
                   download::DownloadItem::InsecureDownloadStatus::SAFE ||
               item->GetInsecureDownloadStatus() ==
                   download::DownloadItem::InsecureDownloadStatus::VALIDATED);
  if (!item || snapshot.state != DownloadState::kComplete ||
      (snapshot.media_type != MediaTopLevelType::kAudio &&
       snapshot.media_type != MediaTopLevelType::kVideo) ||
      snapshot.received_bytes == 0u ||
      snapshot.received_bytes > 16u * 1024u * 1024u ||
      content::DownloadItemUtils::GetOriginalWebContents(item) !=
          web_contents ||
      item->GetState() != download::DownloadItem::COMPLETE ||
      item->IsDangerous() || !safe_insecure_status ||
      item->GetTargetFilePath().empty()) {
    std::move(callback).Run(base::File());
    return;
  }
  const TaskDownloadManagerSnapshot current = Project(*item);
  if (current.download_id != snapshot.download_id ||
      current.state != snapshot.state ||
      current.media_type != snapshot.media_type ||
      current.received_bytes != snapshot.received_bytes) {
    std::move(callback).Run(base::File());
    return;
  }
  content::DownloadManager::GetTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&OpenExactDownloadFile, item->GetTargetFilePath(),
                     snapshot.received_bytes),
      std::move(callback));
}

}  // namespace taffy
