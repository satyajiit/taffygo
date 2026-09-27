// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_throttle_factory.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "taffy/components/filtering/browser/filtering_document_decisions.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/browser/filtering_tab_counters.h"
#include "taffy/components/filtering/browser/filtering_throttle.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {

namespace {

// Runs on the UI thread when a throttle blocked one request: the per-tab
// counter takes it from here. The WebContents is re-resolved rather than
// captured, because the tab may have closed while the request was in flight.
void OnRequestBlocked(
    base::RepeatingCallback<content::WebContents*()> wc_getter,
    base::WeakPtr<FilteringRulesetService> service) {
  content::WebContents* web_contents = wc_getter.Run();
  if (!web_contents) {
    // The tab is gone; the block still happened. The lifetime total keeps it.
    if (service) {
      service->NoteBlocked();
    }
    return;
  }
  FilteringTabCounters::CreateForWebContents(web_contents, service);
  FilteringTabCounters::FromWebContents(web_contents)->NoteBlocked();
}

}  // namespace

void AppendFilteringThrottles(
    base::WeakPtr<FilteringRulesetService> service,
    const network::ResourceRequest& request,
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>>& throttles) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!service) {
    return;
  }
  // Top-level documents are never filtered, so their requests carry nothing.
  if (request.destination == network::mojom::RequestDestination::kDocument) {
    return;
  }
  if (!request.url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  scoped_refptr<const SharedRuleset> ruleset = service->ruleset();
  if (!ruleset) {
    return;
  }
  content::WebContents* web_contents = wc_getter.Run();
  if (!web_contents) {
    // A request no tab owns — a service worker's own fetch, a prefetch with
    // no contents. Judged not at all rather than under an invented document.
    return;
  }
  const GURL document_url = web_contents->GetLastCommittedURL();
  const url::Origin document_origin =
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  FilteringDocumentDecisions::CreateForWebContents(web_contents);
  FilteringDocumentDecision decision =
      FilteringDocumentDecisions::FromWebContents(web_contents)
          ->Resolve(*service, ruleset, document_url, document_origin);
  if (!decision.active) {
    return;
  }
  // The counter exists from the first filtered request, not the first block,
  // so its page-change reset is armed before anything is counted.
  FilteringTabCounters::CreateForWebContents(web_contents, service);
  throttles.push_back(std::make_unique<FilteringThrottle>(
      std::move(ruleset), document_origin, decision.disable_generic_rules,
      base::BindPostTask(
          content::GetUIThreadTaskRunner({}),
          base::BindRepeating(&OnRequestBlocked, wc_getter, service))));
}

}  // namespace taffy::filtering
