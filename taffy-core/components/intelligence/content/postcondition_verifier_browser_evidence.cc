// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser-owned half of PostconditionVerifier: Chromium's own navigation,
// tab and flow records, and the endings they force. Split from
// postcondition_verifier.cc along the section the class already drew; the
// observed evidence and the decision stay there.

#include "taffy/components/intelligence/content/postcondition_verifier.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/process/kill.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/components/intelligence/content/task_navigation_refusal.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

// The URL projection the verifier compares against. Full addresses are kept
// only when a policy-bound postcondition already disclosed one; other flows
// retain the older origin-and-path projection and do not collect query bytes.
UrlMetadata BuildUrlMetadata(const GURL& url,
                             const Origin& origin,
                             UrlDisclosure disclosure) {
  UrlMetadata metadata;
  metadata.origin = origin;
  metadata.disclosure = disclosure;
  if (disclosure == UrlDisclosure::kOriginAndPath) {
    metadata.path = url.path();
  } else if (disclosure == UrlDisclosure::kFullUrl) {
    metadata.url = url.spec();
  }
  metadata.has_query = url.has_query();
  metadata.has_fragment = url.has_ref();
  return metadata;
}

}  // namespace

// --- browser-owned evidence -------------------------------------------------

void PostconditionVerifier::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (finished_ || !handle) {
    return;
  }
  // An action's navigation is the tab's own: a task may start no other
  // (`TaskNavigationThrottle` refuses one that is not in the primary main
  // frame), so a frame inside the page committing is not evidence of anything
  // the action did. This observer used to judge every commit in the tab. A
  // results page's sandboxed frame committing an opaque document while a
  // followed link was still loading contradicted the link as
  // `kDestinationChanged` - and the main frame then landed where the link
  // said, on a page the task held no source for. A frame committing at the
  // declared origin would have verified a navigation that had not happened
  // (decision 0230).
  if (!handle->IsInPrimaryMainFrame()) {
    return;
  }
  if (!handle->HasCommitted()) {
    NoteNavigationThatDidNotCommit(*handle);
    return;
  }

  // Chromium's own navigation record. That is the entire reason this evidence
  // is trustworthy: nothing here came from the renderer.
  const GURL committed_url = handle->GetURL();
  const url::Origin committed_origin = url::Origin::Create(committed_url);

  navigation_.committed = true;
  navigation_.is_same_document = handle->IsSameDocument();
  navigation_.is_in_primary_main_frame = handle->IsInPrimaryMainFrame();
  navigation_.is_error_page = handle->IsErrorPage();
  navigation_.committed_origin =
      OriginCodec::Get().ToWireOrigin(committed_origin);
  const bool full_address_disclosed = std::any_of(
      postconditions_.begin(), postconditions_.end(),
      [](const Postcondition& postcondition) {
        return postcondition.expected_destination &&
               postcondition.expected_destination->url_metadata.disclosure ==
                   UrlDisclosure::kFullUrl;
      });
  navigation_.committed_url =
      BuildUrlMetadata(committed_url, navigation_.committed_origin,
                       full_address_disclosed ? UrlDisclosure::kFullUrl
                                              : UrlDisclosure::kOriginAndPath);
  navigation_.redirect_chain.clear();
  for (const GURL& redirect_url : handle->GetRedirectChain()) {
    const Origin redirect_origin =
        OriginCodec::Get().ToWireOrigin(url::Origin::Create(redirect_url));
    navigation_.redirect_chain.push_back(NavigationHopEvidence{
        .origin = redirect_origin,
        .url = BuildUrlMetadata(redirect_url, redirect_origin,
                                full_address_disclosed
                                    ? UrlDisclosure::kFullUrl
                                    : UrlDisclosure::kOriginAndPath),
    });
  }
  if (broker_ && handle->GetRenderFrameHost()) {
    navigation_.frame_id =
        broker_->GetOrAssignFrameId(handle->GetRenderFrameHost());
  }

  // A cross-document commit nobody declared contradicts every observation-based
  // postcondition at once: whatever was going to be observed is gone, and the
  // action's effect can no longer be established either way.
  if (!Declares(PostconditionKind::kCommittedNavigation) &&
      !Declares(PostconditionKind::kSearchResultState) &&
      !navigation_.is_same_document) {
    NoteVerifier(VerifierKind::kBrowserNavigationEvent);
    Finish(ActionResultCode::kCancelledByNavigation, std::nullopt,
           VerifierOutcome::kContradicted);
    return;
  }
  Evaluate();
}

// The navigation this action started, remembered by Chromium's own id.
//
// A task's navigation is browser-initiated, cross-document and in the primary
// main frame, and the action that starts one declares a committed navigation.
// Nothing a page does has that shape: a renderer may not begin a navigation
// under task authority, and `TaskNavigationThrottle` refuses it if it tries.
// The first such navigation after this verifier begins observing is the one
// the dispatcher just asked for.
//
// Matching on the id rather than on the address is the point. The declared
// destination carries a URL only when its disclosure is `kFullUrl`, and for a
// followed link it is often `kOriginAndPath`, so an address comparison
// silently matched nothing and the phone went on waiting out its ten seconds
// exactly as before (decision 0227).
void PostconditionVerifier::DidStartNavigation(
    content::NavigationHandle* handle) {
  if (finished_ || !handle || own_navigation_id_.has_value() ||
      !Declares(PostconditionKind::kCommittedNavigation) ||
      handle->IsRendererInitiated() || !handle->IsInPrimaryMainFrame() ||
      handle->IsSameDocument()) {
    return;
  }
  own_navigation_id_ = handle->GetNavigationId();
}

// That navigation, ending without committing.
//
// The browser refuses a task's navigation for reasons of its own — a redirect
// off https, a landing in a restricted destination class, a hop the authority
// does not reach — and the throttle cancels the request. A cancelled request
// commits nothing, so this observer used to return at the `HasCommitted()`
// guard and the verifier went on waiting for an effect that had already been
// decided against. Ten seconds later the action came back
// `kPostconditionTimeout`, which says the effect *may still be pending*, and
// the task recorded `OUTCOME_UNKNOWN` and handed the work back to the person
// under the message "Taffy stopped unexpectedly".
//
// None of that was unknown. Measured on a phone: an errand reached
// `myaadhaarbeta.uidai.gov.in`, followed a link, the site answered with a
// redirect to plain http, the throttle cancelled it at `why=hop-left-https`
// 81 ms later, and the task then sat for the remaining 9.9 seconds before
// giving up in a way it could not recover from (decision 0227).
void PostconditionVerifier::NoteNavigationThatDidNotCommit(
    content::NavigationHandle& handle) {
  if (!own_navigation_id_.has_value() ||
      handle.GetNavigationId() != *own_navigation_id_) {
    return;
  }
  NoteVerifier(VerifierKind::kBrowserNavigationEvent);
  // The browser refused it, and said which refusal. That is settled and it is
  // not unknown: the request never reached a document, so the code is the
  // refusal's own, whose side effect is "not performed", and the task tells
  // the model and lets it try another way.
  //
  // `kPostconditionFailed` said the same thing to this class and something
  // else to the task. The core classes it as an effect that may have landed,
  // so the action went to `OUTCOME_UNKNOWN`, reconciliation asked the browser,
  // the browser answered 25 again, and the task handed the work to the person
  // 21 ms later without the model seeing the refusal at all (decision 0228).
  if (const TaskNavigationRefusal* refusal =
          TaskNavigationRefusal::GetForNavigationHandle(handle)) {
    Finish(refusal->code(), std::nullopt, VerifierOutcome::kContradicted);
    return;
  }
  // Uncommitted for a reason the browser did not choose: overtaken, or a
  // response that became a download. Deliberately not
  // `kCancelledByNavigation`, which carries different advice; and still a
  // contradiction whose side effect is not known, because this class cannot
  // tell what the server did with the request.
  Finish(ActionResultCode::kPostconditionFailed, std::nullopt,
         VerifierOutcome::kContradicted);
}

void PostconditionVerifier::DidStopLoading() {
  if (finished_) {
    return;
  }
  navigation_.loading_stopped = true;
  Evaluate();
}

void PostconditionVerifier::DidOpenRequestedURL(
    content::WebContents* new_contents,
    content::RenderFrameHost* source_render_frame_host,
    const GURL& url,
    const content::Referrer& referrer,
    WindowOpenDisposition disposition,
    ui::PageTransition transition,
    bool started_from_context_menu,
    bool renderer_initiated) {
  if (finished_ || !new_contents) {
    return;
  }
  // VERIFY AT SP-01: DidOpenRequestedURL's parameter list and
  // WebContents::HasOpener() at the pinned milestone. This is the browser's own
  // record that a new navigable was created for a request from this tab, which
  // is what makes the new-tab postcondition verifiable at all; a renderer's
  // claim that window.open succeeded would not be.
  const url::Origin destination = url::Origin::Create(url);
  tab_.tab_created = true;
  tab_.destination_origin = OriginCodec::Get().ToWireOrigin(destination);
  const bool full_address_disclosed = std::any_of(
      postconditions_.begin(), postconditions_.end(),
      [](const Postcondition& postcondition) {
        return postcondition.expected_destination &&
               postcondition.expected_destination->url_metadata.disclosure ==
                   UrlDisclosure::kFullUrl;
      });
  tab_.destination_url =
      BuildUrlMetadata(url, tab_.destination_origin,
                       full_address_disclosed ? UrlDisclosure::kFullUrl
                                              : UrlDisclosure::kOriginAndPath);
  tab_.has_opener_reference = new_contents->HasOpener();
  tab_.opener_tab_id = broker_ ? broker_->tab_id() : TabId();
  Evaluate();
}

void PostconditionVerifier::OnTaskTabCreated(content::WebContents* new_contents,
                                             const GURL& url) {
  DidOpenRequestedURL(new_contents, /*source_render_frame_host=*/nullptr, url,
                      content::Referrer(),
                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                      ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                      /*started_from_context_menu=*/false,
                      /*renderer_initiated=*/false);
  if (!finished_ && new_contents) {
    Observe(new_contents);
  }
}

void PostconditionVerifier::OnBrowserFlowStarted(
    const BrowserFlowEvidence& evidence) {
  if (finished_) {
    return;
  }
  flow_ = evidence;
  Evaluate();
}

void PostconditionVerifier::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // The renderer died while the effect was in question. Never a verified
  // result, and never an automatic replay for a consequential action
  // (protocol section 13).
  Finish(RepeatMayDuplicateEffect(idempotency_)
             ? ActionResultCode::kOutcomeUnknown
             : ActionResultCode::kRendererCrashed,
         std::nullopt, VerifierOutcome::kCancelled);
}

void PostconditionVerifier::WebContentsDestroyed() {
  Finish(RepeatMayDuplicateEffect(idempotency_)
             ? ActionResultCode::kOutcomeUnknown
             : ActionResultCode::kTabGone,
         std::nullopt, VerifierOutcome::kCancelled);
  Observe(nullptr);
}

void PostconditionVerifier::CancelWith(ActionResultCode code) {
  Finish(code, std::nullopt, VerifierOutcome::kCancelled);
}

void PostconditionVerifier::OnDeadline() {
  // The declared effect was not observed in time. Not a failure claim: the
  // effect may still be pending, which is why the code is a timeout and why
  // the record says whether repeating it could duplicate the effect.
  Finish(ActionResultCode::kPostconditionTimeout, std::nullopt,
         VerifierOutcome::kTimedOut);
}

}  // namespace taffy
