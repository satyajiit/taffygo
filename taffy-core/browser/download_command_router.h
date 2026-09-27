// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_DOWNLOAD_COMMAND_ROUTER_H_
#define TAFFY_BROWSER_DOWNLOAD_COMMAND_ROUTER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/browser/download_record.h"
#include "taffy/browser/download_state_machine.h"
#include "taffy/common/public/bip_identity.h"

// The one entry point for a download control (PAR-FILE-001, CAP-DL-001).
//
// Three things happen here and nowhere else:
//
//   1. **The command is checked against the state.** The matrix lives in
//      download_state_machine.h; this class applies it, so no surface has to
//      decide for itself whether cancelling a finished download makes sense.
//   2. **The origin of the command is checked.** A download control is a user
//      control. The assistant has no capability to start, retry or open a
//      download before the M5 exit review, and the design is
//      [Open (OD-056)] — so an assistant-attributed command is refused here,
//      in the browser process, where a compromised renderer cannot reach past
//      it.
//   3. **A retry is re-decided, not repeated.** Retrying starts a fresh
//      network request, so it goes back through DownloadIntentRouter
//      (public/taffy_download_intent.h) exactly as the original download did.
//      A control that bypassed that check would be a way to launder an
//      assistant-initiated download through a user-initiated one.
//
// Everything that actually moves bytes is Chromium's, behind
// DownloadCommandDelegate. This class never touches the file system, never
// picks a target path, never uniquifies a name and never overrides a danger
// decision.
//
// UI thread only.

namespace taffy {

// Who asked for the control. Deliberately a separate vocabulary from
// NavigationInitiator: that names who started the *download*, this names who
// pressed the button, and a user pressing cancel on a page-initiated download
// is an ordinary and important case.
enum class DownloadCommandOrigin : uint8_t {
  kUnknown = 0,
  kUserGesture = 1,
  kAssistantTask = 2,
  // The browser itself — a profile shutting down, a policy cancelling a
  // transfer. Never subject to the assistant refusal, always subject to the
  // state matrix.
  kBrowser = 3,
};

enum class DownloadCommandResult : uint8_t {
  kExecuted = 0,
  kRefusedNoDelegate = 1,
  kRefusedUnknownDownload = 2,
  kRefusedUnattributedOrigin = 3,
  // The assistant does not control downloads before M5; see [Open (OD-056)].
  kRefusedAssistantInitiated = 4,
  kRefusedIllegalInState = 5,
  kRefusedNotResumable = 6,
  kRefusedNeedsDangerConfirmation = 7,
  // The retry was re-decided by the download and external-intent seam and did
  // not pass. The reason is that seam's DownloadDecision, which the caller can
  // read from the same facts it supplied.
  kRefusedByIntentRouter = 8,
  kPlatformRefused = 9,
};

// Implemented by the browser layer over Chromium's download system. Every
// method performs and none of them decides; the router has already refused
// anything that should not reach here.
//
// VERIFY AT SP-01: the download::DownloadItem methods these map onto at the
// pinned milestone. Upstream file to read:
// components/download/public/common/download_item.h — Pause(), Resume(bool
// user_gesture), Cancel(bool user_cancel), OpenDownload() and
// SetOpenWhenComplete(bool). A retry is not a DownloadItem method: it is a new
// download request, which is why RetryDownload takes the record rather than an
// item handle.
class DownloadCommandDelegate {
 public:
  virtual ~DownloadCommandDelegate() = default;

  virtual bool Pause(uint32_t download_id) = 0;
  virtual bool Resume(uint32_t download_id, bool user_gesture) = 0;
  virtual bool Cancel(uint32_t download_id) = 0;
  virtual bool OpenWhenComplete(uint32_t download_id) = 0;
  virtual bool OpenNow(uint32_t download_id) = 0;

  // Starts a fresh download of the same resource. Returns false when the
  // platform declined.
  virtual bool Retry(const DownloadRecord& record) = 0;
};

class DownloadCommandRouter {
 public:
  DownloadCommandRouter();
  DownloadCommandRouter(const DownloadCommandRouter&) = delete;
  DownloadCommandRouter& operator=(const DownloadCommandRouter&) = delete;
  ~DownloadCommandRouter();

  // Null clears it. With no delegate every command is refused and every query
  // still answers from the record.
  void SetDelegate(DownloadCommandDelegate* delegate);

  DownloadCommandResult Execute(uint32_t download_id,
                                DownloadCommand command,
                                DownloadCommandOrigin command_origin);

  // Called from Chromium's download observer as the item changes. The record
  // is a copy of the download system's facts; this class never edits one to
  // reflect a command it just issued, because the download system is the
  // authority on what happened.
  void NoteDownloadChanged(const DownloadRecord& record);
  void NoteDownloadRemoved(uint32_t download_id);

  const DownloadRecord* Find(uint32_t download_id) const;
  std::vector<DownloadRecord> ListForTab(const TabId& tab_id) const;
  size_t record_count() const { return records_.size(); }

 private:
  raw_ptr<DownloadCommandDelegate> delegate_ = nullptr;
  std::map<uint32_t, DownloadRecord> records_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_DOWNLOAD_COMMAND_ROUTER_H_
