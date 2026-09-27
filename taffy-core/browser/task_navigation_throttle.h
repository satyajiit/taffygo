// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_NAVIGATION_THROTTLE_H_
#define TAFFY_BROWSER_TASK_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"
#include "taffy/browser/task_navigation_authority.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace taffy {

// Enforces the exact network boundary of a browser-issued task navigation.
// It is created only for NavigationHandles carrying TaskNavigationAuthority,
// so ordinary manual browsing pays no per-navigation throttle allocation.
class TaskNavigationThrottle final : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  TaskNavigationThrottle(content::NavigationThrottleRegistry& registry,
                         TaskNavigationAuthority authority);
  TaskNavigationThrottle(const TaskNavigationThrottle&) = delete;
  TaskNavigationThrottle& operator=(const TaskNavigationThrottle&) = delete;
  ~TaskNavigationThrottle() override;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  ThrottleCheckResult WillProcessResponse() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult CheckRequest(bool is_redirect);
  // Cancels the request, first telling the action's verifier which refusal
  // it was (decision 0228).
  ThrottleCheckResult Refuse(TaskNavigationAuthority::RequestVerdict verdict);

  const TaskNavigationAuthority authority_;
  // Set while the request stands on an http hop the authority would allow
  // over https, and cleared by the hop that returns it to https. Chromium
  // upgrades such a hop before sending it when the host is known to be
  // https-only, so it is followed; a response that still arrives over http
  // is refused before anything commits (decision 0228).
  bool awaiting_upgrade_ = false;
};

// Refuses a renderer/browser navigation that began inside an assistant
// dispatch window if Chromium later classifies its response as a download.
// The existence of this throttle is the latched causal fact: later user input
// cannot relabel a navigation that the assistant had already started.
class TaskActionDownloadThrottle final : public content::NavigationThrottle {
 public:
  explicit TaskActionDownloadThrottle(
      content::NavigationThrottleRegistry& registry);
  TaskActionDownloadThrottle(const TaskActionDownloadThrottle&) = delete;
  TaskActionDownloadThrottle& operator=(const TaskActionDownloadThrottle&) =
      delete;
  ~TaskActionDownloadThrottle() override;

  ThrottleCheckResult WillProcessResponse() override;
  const char* GetNameForLogging() override;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_NAVIGATION_THROTTLE_H_
