// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/filtering_request_filter.h"

#include <memory>
#include <optional>
#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/filtering/browser/filtering_document_decisions.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/browser/filtering_tab_counters.h"
#include "taffy/components/filtering/browser/filtering_throttle.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

class FilteringRequestFilter final
    : public filtering::mojom::RequestFilter {
 public:
  explicit FilteringRequestFilter(int render_process_id)
      : render_process_id_(render_process_id) {}

  FilteringRequestFilter(const FilteringRequestFilter&) = delete;
  FilteringRequestFilter& operator=(const FilteringRequestFilter&) = delete;

  ~FilteringRequestFilter() override = default;

  void Check(const std::optional<blink::LocalFrameToken>& frame_token,
             const GURL& request_url,
             network::mojom::RequestDestination destination,
             CheckCallback callback) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    content::WebContents* web_contents = WebContentsFor(frame_token);
    if (!web_contents || !request_url.SchemeIsHTTPOrHTTPS() ||
        destination == network::mojom::RequestDestination::kDocument) {
      std::move(callback).Run(false);
      return;
    }

    Profile* profile =
        Profile::FromBrowserContext(web_contents->GetBrowserContext());
    CoreServiceManager* manager =
        profile ? CoreServiceManagerFactory::GetForProfileIfExists(profile)
                : nullptr;
    filtering::FilteringRulesetService* service =
        manager ? manager->filtering_service() : nullptr;
    scoped_refptr<const filtering::SharedRuleset> ruleset =
        service ? service->ruleset() : nullptr;
    if (!service || !ruleset) {
      std::move(callback).Run(false);
      return;
    }

    const GURL document_url = web_contents->GetLastCommittedURL();
    const url::Origin document_origin =
        web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();
    filtering::FilteringDocumentDecisions::CreateForWebContents(web_contents);
    const filtering::FilteringDocumentDecision decision =
        filtering::FilteringDocumentDecisions::FromWebContents(web_contents)
            ->Resolve(*service, ruleset, document_url, document_origin);
    const bool blocked =
        decision.active &&
        ruleset->matcher().ShouldBlockRequest(
            request_url, document_origin,
            filtering::ElementTypeForDestination(destination),
            decision.disable_generic_rules);
    if (blocked) {
      filtering::FilteringTabCounters::CreateForWebContents(
          web_contents, service->GetWeakPtr());
      filtering::FilteringTabCounters::FromWebContents(web_contents)
          ->NoteBlocked();
    }
    std::move(callback).Run(blocked);
  }

  void Clone(
      mojo::PendingReceiver<filtering::mojom::RequestFilter> receiver) override {
    BindFilteringRequestFilter(render_process_id_, std::move(receiver));
  }

 private:
  content::WebContents* WebContentsFor(
      const std::optional<blink::LocalFrameToken>& frame_token) const {
    if (!frame_token) {
      return nullptr;
    }
    content::RenderFrameHost* frame = content::RenderFrameHost::FromFrameToken(
        content::GlobalRenderFrameHostToken(render_process_id_, *frame_token));
    return frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  }

  const int render_process_id_;
};

}  // namespace

void BindFilteringRequestFilter(
    int render_process_id,
    mojo::PendingReceiver<filtering::mojom::RequestFilter> receiver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<FilteringRequestFilter>(render_process_id),
      std::move(receiver));
}

}  // namespace taffy
