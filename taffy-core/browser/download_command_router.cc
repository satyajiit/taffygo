// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/download_command_router.h"

#include "taffy/browser/taffy_download_intent_router.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

DownloadCommandResult ResultFor(DownloadCommandLegality legality) {
  switch (legality) {
    case DownloadCommandLegality::kLegal:
      return DownloadCommandResult::kExecuted;
    case DownloadCommandLegality::kIllegalInState:
      return DownloadCommandResult::kRefusedIllegalInState;
    case DownloadCommandLegality::kNotResumable:
      return DownloadCommandResult::kRefusedNotResumable;
    case DownloadCommandLegality::kNeedsDangerConfirmation:
      return DownloadCommandResult::kRefusedNeedsDangerConfirmation;
  }
}

}  // namespace

DownloadCommandRouter::DownloadCommandRouter() = default;
DownloadCommandRouter::~DownloadCommandRouter() = default;

void DownloadCommandRouter::SetDelegate(DownloadCommandDelegate* delegate) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  delegate_ = delegate;
}

DownloadCommandResult DownloadCommandRouter::Execute(
    uint32_t download_id,
    DownloadCommand command,
    DownloadCommandOrigin command_origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const DownloadRecord* record = Find(download_id);
  if (!record) {
    return DownloadCommandResult::kRefusedUnknownDownload;
  }

  switch (command_origin) {
    case DownloadCommandOrigin::kUnknown:
      // Fail closed, the same rule as every other seam in this directory.
      return DownloadCommandResult::kRefusedUnattributedOrigin;
    case DownloadCommandOrigin::kAssistantTask:
      // The assistant controls no download before the M5 exit review; the
      // design is [Open (OD-056)]. Refusing here means the rule holds even if
      // a renderer is compromised or a future call site forgets it.
      return DownloadCommandResult::kRefusedAssistantInitiated;
    case DownloadCommandOrigin::kUserGesture:
    case DownloadCommandOrigin::kBrowser:
      break;
  }

  const DownloadCommandLegality legality =
      EvaluateDownloadCommand(*record, command);
  if (legality != DownloadCommandLegality::kLegal) {
    return ResultFor(legality);
  }

  if (DownloadCommandStartsANewRequest(command)) {
    // A retry is a fresh network request, so it is a fresh decision. Sending
    // it back through the download and external-intent seam is what stops a
    // control from laundering an assistant-initiated download into a
    // user-initiated one.
    DownloadRequestFacts facts;
    facts.tab_id = record->tab_id;
    facts.initiator = command_origin == DownloadCommandOrigin::kUserGesture
                          ? NavigationInitiator::kUser
                          : record->initiator;
    facts.has_user_activation =
        command_origin == DownloadCommandOrigin::kUserGesture;
    if (GetDownloadIntentRouter().EvaluateDownload(facts) !=
        DownloadDecision::kAllowOrdinaryPath) {
      return DownloadCommandResult::kRefusedByIntentRouter;
    }
  }

  if (!delegate_) {
    return DownloadCommandResult::kRefusedNoDelegate;
  }

  const bool user_gesture =
      command_origin == DownloadCommandOrigin::kUserGesture;
  bool performed = false;
  switch (command) {
    case DownloadCommand::kPause:
      performed = delegate_->Pause(download_id);
      break;
    case DownloadCommand::kResume:
      performed = delegate_->Resume(download_id, user_gesture);
      break;
    case DownloadCommand::kCancel:
      performed = delegate_->Cancel(download_id);
      break;
    case DownloadCommand::kRetry:
      performed = delegate_->Retry(*record);
      break;
    case DownloadCommand::kOpenWhenComplete:
      performed = delegate_->OpenWhenComplete(download_id);
      break;
    case DownloadCommand::kOpenNow:
      performed = delegate_->OpenNow(download_id);
      break;
  }

  // The record is not edited here. Whether the state actually changed is the
  // download system's answer, and it arrives through NoteDownloadChanged; a
  // router that optimistically wrote the state it expected would be the second
  // source of truth this class exists to avoid.
  return performed ? DownloadCommandResult::kExecuted
                   : DownloadCommandResult::kPlatformRefused;
}

void DownloadCommandRouter::NoteDownloadChanged(const DownloadRecord& record) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  records_.insert_or_assign(record.download_id, record);
}

void DownloadCommandRouter::NoteDownloadRemoved(uint32_t download_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  records_.erase(download_id);
}

const DownloadRecord* DownloadCommandRouter::Find(uint32_t download_id) const {
  auto it = records_.find(download_id);
  return it == records_.end() ? nullptr : &it->second;
}

std::vector<DownloadRecord> DownloadCommandRouter::ListForTab(
    const TabId& tab_id) const {
  std::vector<DownloadRecord> listed;
  for (const auto& [download_id, record] : records_) {
    if (record.tab_id == tab_id) {
      listed.push_back(record);
    }
  }
  return listed;
}

}  // namespace taffy
