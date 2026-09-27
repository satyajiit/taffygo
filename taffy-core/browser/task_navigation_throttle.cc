// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_navigation_throttle.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/logging.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/task_navigation_refusal.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace taffy {

// static
void TaskNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  std::optional<TaskNavigationAuthority> authority;
  if (handle.IsInPrimaryMainFrame()) {
    authority = TaskNavigationAuthority::FromNavigation(handle);
  }
  LOG(WARNING) << "[taffy_task_navigation_throttle] authority="
               << (authority ? 1 : 0)
               << " primary=" << (handle.IsInPrimaryMainFrame() ? 1 : 0);
  if (authority) {
    registry.AddThrottle(std::make_unique<TaskNavigationThrottle>(
        registry, std::move(*authority)));
    return;
  }

  content::WebContents* web_contents = handle.GetWebContents();
  TaffyPageIntelligenceHost* host =
      web_contents ? TaffyPageIntelligenceHost::FromWebContents(web_contents)
                   : nullptr;
  if (host && host->HasOpenAssistantDispatch()) {
    // This check happens when the NavigationHandle is created. Latching the
    // fact here prevents a person's later input, or a later task dispatch,
    // from changing who started this particular request by the time response
    // headers reveal that it is a download.
    registry.AddThrottle(
        std::make_unique<TaskActionDownloadThrottle>(registry));
  }
}

TaskNavigationThrottle::TaskNavigationThrottle(
    content::NavigationThrottleRegistry& registry,
    TaskNavigationAuthority authority)
    : content::NavigationThrottle(registry), authority_(std::move(authority)) {}

TaskNavigationThrottle::~TaskNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
TaskNavigationThrottle::WillStartRequest() {
  return CheckRequest(/*is_redirect=*/false);
}

content::NavigationThrottle::ThrottleCheckResult
TaskNavigationThrottle::WillRedirectRequest() {
  return CheckRequest(/*is_redirect=*/true);
}

content::NavigationThrottle::ThrottleCheckResult
TaskNavigationThrottle::WillProcessResponse() {
  content::NavigationHandle* handle = navigation_handle();
  if (handle && awaiting_upgrade_) {
    // The http hop was not upgraded, so this response came over plain http.
    // A task never reads or acts on such a document; refused here, before
    // the response reaches a renderer or commits.
    LOG(WARNING) << "[taffy_task_navigation_cancelled] at=response"
                 << " why=response-over-http redirect=1";
    return Refuse(TaskNavigationAuthority::RequestVerdict::kRefusedInsecureHop);
  }
  if (!handle || handle->IsDownload()) {
    // A task authorized a navigation, not a transfer. Chromium knows whether
    // the response is a download only after the final response headers arrive;
    // refusing here keeps the response out of DownloadManager and prevents a
    // server from laundering Navigate into StartDownload. The separately
    // authorized browser.download.start path does not create a navigation and
    // therefore does not cross this throttle.
    return CANCEL_AND_IGNORE;
  }
  return PROCEED;
}

TaskActionDownloadThrottle::TaskActionDownloadThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

TaskActionDownloadThrottle::~TaskActionDownloadThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
TaskActionDownloadThrottle::WillProcessResponse() {
  content::NavigationHandle* handle = navigation_handle();
  return !handle || handle->IsDownload() ? CANCEL_AND_IGNORE : PROCEED;
}

const char* TaskActionDownloadThrottle::GetNameForLogging() {
  return "TaskActionDownloadThrottle";
}

const char* TaskNavigationThrottle::GetNameForLogging() {
  return "TaskNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
TaskNavigationThrottle::CheckRequest(bool is_redirect) {
  content::NavigationHandle* handle = navigation_handle();
  const char* at = nullptr;
  if (!handle) {
    at = "no-handle";
  } else if (!handle->IsInPrimaryMainFrame()) {
    at = "not-primary-main-frame";
  } else if (handle->IsRendererInitiated()) {
    at = "renderer-initiated";
  }
  if (at) {
    // A cancelled request commits nothing. These three clauses each say the
    // whole of themselves, and none is a navigation an action's verifier
    // waits on, so nothing is carried on the handle (decision 0177).
    LOG(WARNING) << "[taffy_task_navigation_cancelled] at=" << at
                 << " why=n/a redirect=" << (is_redirect ? 1 : 0);
    return CANCEL_AND_IGNORE;
  }

  TaskNavigationAuthority::RequestVerdict verdict =
      authority_.VerdictForRequest(handle->GetURL(), is_redirect);
  if (verdict == TaskNavigationAuthority::RequestVerdict::kRefusedInsecureHop &&
      handle->GetURL().SchemeIs(url::kHttpScheme)) {
    // A site that answers over https with a redirect to its own http address
    // is common, and in this browser it is not a downgrade: a host that has
    // told Chromium it is https-only is upgraded before the hop is sent, and
    // the upgrade arrives here as one more redirect, back onto https. So the
    // hop is judged as its https form would be and followed if that is
    // allowed, and `WillProcessResponse` refuses the request if it is still
    // on http when the answer comes. Measured on a phone: a followed link on
    // a `uidai.gov.in` site was answered this way, and cancelling it here
    // ended the errand (decision 0228).
    GURL::Replacements secure;
    secure.SetSchemeStr(url::kHttpsScheme);
    const TaskNavigationAuthority::RequestVerdict upgraded =
        authority_.VerdictForRequest(
            handle->GetURL().ReplaceComponents(secure), is_redirect);
    if (TaskNavigationAuthority::IsAllowed(upgraded)) {
      awaiting_upgrade_ = true;
      LOG(WARNING) << "[taffy_task_navigation_hop] awaiting=https-upgrade";
      return PROCEED;
    }
    verdict = upgraded;
  }
  if (TaskNavigationAuthority::IsAllowed(verdict)) {
    awaiting_upgrade_ = false;
    return PROCEED;
  }
  // `at=not-the-authorized-destination` stands for six rules, and a phone
  // spent ten seconds on each of them without saying which. `why=` is the
  // clause. It is never the address (decision 0226).
  LOG(WARNING) << "[taffy_task_navigation_cancelled]"
               << " at=not-the-authorized-destination"
               << " why=" << TaskNavigationAuthority::NameOfVerdict(verdict)
               << " redirect=" << (is_redirect ? 1 : 0);
  return Refuse(verdict);
}

content::NavigationThrottle::ThrottleCheckResult TaskNavigationThrottle::Refuse(
    TaskNavigationAuthority::RequestVerdict verdict) {
  // Both codes are refusals before anything reached a document. The class
  // table's gets its own because its advice differs: that is a site this
  // product never acts on for a person, and the model is told to hand the
  // step over rather than to look for another way there.
  if (content::NavigationHandle* handle = navigation_handle()) {
    TaskNavigationRefusal::CreateForNavigationHandle(
        *handle,
        verdict ==
                TaskNavigationAuthority::RequestVerdict::kRefusedRestrictedClass
            ? ActionResultCode::kDestinationClassRestricted
            : ActionResultCode::kEgressNotAuthorized);
  }
  return CANCEL_AND_IGNORE;
}

}  // namespace taffy
