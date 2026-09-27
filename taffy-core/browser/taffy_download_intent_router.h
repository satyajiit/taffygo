// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TAFFY_DOWNLOAD_INTENT_ROUTER_H_
#define TAFFY_BROWSER_TAFFY_DOWNLOAD_INTENT_ROUTER_H_

#include "taffy/common/public/taffy_download_intent.h"

namespace content {
class WebContents;
}  // namespace content

// The process-wide download and external-intent router
// (public/taffy_download_intent.h explains the rule it enforces).

namespace taffy {

// Never null. Safe to call from the UI thread after browser-process startup.
DownloadIntentRouter& GetDownloadIntentRouter();

// Whether a renderer's direct content-initiated download must be refused
// because it was requested while this exact tab had an assistant dispatch in
// flight. Ordinary page/user downloads return false and continue through
// Chromium's normal checks. A null WebContents fails closed.
//
// Navigation responses are not classified here: TaskActionDownloadThrottle
// latches their initiator when the NavigationHandle is created, before a later
// dispatch can be confused with an earlier human navigation.
bool ShouldRefuseContentInitiatedDownload(content::WebContents* web_contents);

}  // namespace taffy

#endif  // TAFFY_BROWSER_TAFFY_DOWNLOAD_INTENT_ROUTER_H_
