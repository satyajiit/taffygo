// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/taffy_download_intent_router.h"

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace {

class DownloadIntentRouterImpl : public DownloadIntentRouter {
 public:
  DownloadIntentRouterImpl() = default;
  ~DownloadIntentRouterImpl() override = default;

  void SetDelegate(DownloadIntentDelegate* delegate) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    delegate_ = delegate;
  }

  DownloadDecision EvaluateDownload(
      const DownloadRequestFacts& facts) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    switch (facts.initiator) {
      case NavigationInitiator::kAssistant:
        if (!facts.task_id.is_valid()) {
          return DownloadDecision::kRefusedAssistantInitiated;
        }
        if (!facts.actor_lease_id.is_valid()) {
          return DownloadDecision::kRefusedNoActorLease;
        }
        if (!facts.capability_reference.is_valid() ||
            !facts.dispatch_id.is_valid() || !facts.capability_admitted) {
          return DownloadDecision::kRefusedNoCapability;
        }
        return DownloadDecision::kAllowOrdinaryPath;

      case NavigationInitiator::kUnknown:
        // Fail closed. An initiator that could not be attributed is refused
        // for anything consequential.
        return DownloadDecision::kRefusedUnattributedInitiator;

      case NavigationInitiator::kUser:
      case NavigationInitiator::kPage:
        break;
    }

    // A task holding a lease on the tab does not make a person-initiated
    // download an assistant download, and it does not make it safer either.
    // The check that matters is above; this one only catches a lease that has
    // gone away underneath a request that claimed one.
    if (facts.task_id.is_valid() && !facts.actor_lease_id.is_valid()) {
      return DownloadDecision::kRefusedNoActorLease;
    }

    // Continue into Chromium's ordinary download path, prompts and all. This
    // seam runs before them and can only say no; it never bypasses a file
    // chooser, a permission prompt, a download policy check or a TLS
    // interstitial (protocol section 11.5).
    return DownloadDecision::kAllowOrdinaryPath;
  }

  ExternalIntentDecision EvaluateExternalNavigation(
      const ExternalIntentFacts& facts) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    switch (facts.initiator) {
      case NavigationInitiator::kAssistant:
        // Handing a page to another application is a side effect outside the
        // browser's own policy surface. The assistant does not get to cause
        // one before M5.
        return ExternalIntentDecision::kRefusedAssistantInitiated;
      case NavigationInitiator::kUnknown:
        return ExternalIntentDecision::kRefusedUnattributedInitiator;
      case NavigationInitiator::kUser:
      case NavigationInitiator::kPage:
        break;
    }

    if (!delegate_ || !delegate_->CanHandleExternally(facts.target_scheme)) {
      // No application can take this scheme. Keeping it in the browser is the
      // correct outcome, and it is not a block: the navigation still happens,
      // it just happens here.
      return ExternalIntentDecision::kKeepInBrowser;
    }
    return ExternalIntentDecision::kAllowOrdinaryPath;
  }

 private:
  raw_ptr<DownloadIntentDelegate> delegate_ = nullptr;
};

}  // namespace

DownloadIntentRouter& GetDownloadIntentRouter() {
  static base::NoDestructor<DownloadIntentRouterImpl> instance;
  return *instance;
}

bool ShouldRefuseContentInitiatedDownload(content::WebContents* web_contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!web_contents) {
    return true;
  }
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(web_contents);
  if (!host || !host->HasOpenAssistantDispatch()) {
    return false;
  }

  // A renderer download that overlaps a page action has assistant causation
  // but none of the dedicated StartDownload identities. Run that exact shape
  // through the one router instead of duplicating its refusal rule here.
  DownloadRequestFacts facts;
  facts.initiator = NavigationInitiator::kAssistant;
  return GetDownloadIntentRouter().EvaluateDownload(facts) !=
         DownloadDecision::kAllowOrdinaryPath;
}

}  // namespace taffy
