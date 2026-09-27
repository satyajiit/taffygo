// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/browser_navigation_record.h"

#include <utility>

#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "net/http/http_response_headers.h"
#include "net/ssl/ssl_info.h"
#include "url/origin.h"

namespace taffy {

namespace {

NavigationCommitKind CommitKindFor(content::NavigationHandle* handle) {
  // Order matters: a back/forward cache restore and a prerender activation are
  // both "not same document" and both would otherwise read as a new document,
  // and the difference is exactly what the invalidation table turns on.
  //
  // VERIFY AT SP-04: IsServedFromBackForwardCache(),
  // IsPrerenderedPageActivation(), GetReloadType() and GetRestoreType() on
  // content::NavigationHandle at the pinned milestone. Upstream file to read:
  // content/public/browser/navigation_handle.h. If a predicate has moved,
  // every replacement is a browser-owned fact — none of them may be replaced
  // by a renderer signal.
  if (handle->IsServedFromBackForwardCache()) {
    return NavigationCommitKind::kBackForwardCacheRestore;
  }
  if (handle->IsPrerenderedPageActivation()) {
    return NavigationCommitKind::kPrerenderActivation;
  }
  if (handle->IsSameDocument()) {
    return NavigationCommitKind::kSameDocument;
  }
  if (handle->GetReloadType() != content::ReloadType::NONE) {
    return NavigationCommitKind::kReload;
  }
  if (handle->GetRestoreType() != content::RestoreType::kNotRestored) {
    return NavigationCommitKind::kSessionRestore;
  }
  return NavigationCommitKind::kNewDocument;
}

int ResponseCodeFor(content::NavigationHandle* handle) {
  const net::HttpResponseHeaders* headers = handle->GetResponseHeaders();
  return headers ? headers->response_code() : 0;
}

uint32_t CertStatusFor(content::NavigationHandle* handle) {
  // SP-01 done for 152.0.7977.42: GetSSLInfo() is still
  // `const std::optional<net::SSLInfo>&` (navigation_handle.h:582), and
  // SSLInfo::cert_status is still a net::CertStatus defaulting to 0
  // (net/ssl/ssl_info.h:53). What the pin does require is the include above:
  // navigation_handle.h only forward-declares net::SSLInfo (line 58), and
  // std::optional<T> instantiates its storage the moment has_value() is
  // called, so the incomplete type is a hard error rather than a warning. The
  // absent case must stay 0 and not a sentinel — 0 is "no certificate errors"
  // in cert_status_flags.h, and it is the correct answer for a navigation that
  // never negotiated TLS.
  const auto& ssl_info = handle->GetSSLInfo();
  return ssl_info.has_value() ? ssl_info->cert_status : 0u;
}

}  // namespace

BrowserNavigationRecord::BrowserNavigationRecord() = default;
BrowserNavigationRecord::BrowserNavigationRecord(
    const BrowserNavigationRecord&) = default;
BrowserNavigationRecord& BrowserNavigationRecord::operator=(
    const BrowserNavigationRecord&) = default;
BrowserNavigationRecord::BrowserNavigationRecord(BrowserNavigationRecord&&) =
    default;
BrowserNavigationRecord& BrowserNavigationRecord::operator=(
    BrowserNavigationRecord&&) = default;
BrowserNavigationRecord::~BrowserNavigationRecord() = default;

// static
BrowserNavigationRecord BrowserNavigationRecord::FromCommittedNavigation(
    content::NavigationHandle* navigation_handle,
    const TabId& tab_id,
    const FrameId& frame_id) {
  CHECK(navigation_handle);
  CHECK(navigation_handle->HasCommitted())
      << "A navigation record describes a committed navigation. Building one "
         "from an uncommitted handle would record a URL the user never "
         "reached.";

  BrowserNavigationRecord record;
  record.tab_id_ = tab_id;
  record.frame_id_ = frame_id;
  record.committed_at_ = base::TimeTicks::Now();

  // Every value below is read from the navigation handle. Nothing is read from
  // a renderer message, and nothing is read from the document.
  record.committed_url_ = navigation_handle->GetURL();
  record.is_primary_main_frame_ = navigation_handle->IsInPrimaryMainFrame();
  record.is_same_document_ = navigation_handle->IsSameDocument();
  record.had_user_gesture_ = navigation_handle->HasUserGesture();
  record.is_renderer_initiated_ = navigation_handle->IsRendererInitiated();
  record.commit_kind_ = CommitKindFor(navigation_handle);

  OriginCodec& codec = OriginCodec::Get();

  // The committed origin comes from the RenderFrameHost the browser process
  // committed into, not from the URL: a sandboxed document commits an opaque
  // origin that url::Origin::Create(url) would wrongly widen to its tuple
  // predecessor (bip_identity.h, protocol section 5.5).
  content::RenderFrameHost* committed_host =
      navigation_handle->GetRenderFrameHost();
  const url::Origin committed_origin =
      committed_host ? committed_host->GetLastCommittedOrigin()
                     : url::Origin::Create(record.committed_url_);
  record.final_origin_ = codec.ToWireOrigin(committed_origin);

  // The redirect chain, as origins. GetRedirectChain() includes the final URL
  // as its last element, so a direct load produces a one-entry chain.
  //
  // VERIFY AT SP-01: GetRedirectChain() returning const std::vector<GURL>& and
  // including the final URL at the pinned milestone. Upstream file to read:
  // content/public/browser/navigation_handle.h. If the final URL is excluded,
  // append committed_url_ below; the crossed-origin computation is unchanged
  // because it compares against final_origin_.
  const std::vector<GURL>& chain = navigation_handle->GetRedirectChain();
  record.redirect_origins_.reserve(chain.empty() ? 1 : chain.size());
  for (const GURL& url : chain) {
    Origin origin = codec.ToWireOrigin(url::Origin::Create(url));
    if (record.redirect_origins_.empty() ||
        record.redirect_origins_.back() != origin) {
      record.redirect_origins_.push_back(std::move(origin));
    }
  }
  if (record.redirect_origins_.empty()) {
    record.redirect_origins_.push_back(record.final_origin_);
  }

  // The last chain entry is the URL the navigation resolved to, but the
  // committed origin is the authority: a sandboxed or error-page commit ends
  // opaque while its URL is a tuple. Replace the tail so that a consumer
  // walking the chain and a consumer reading final_origin() cannot disagree.
  record.redirect_origins_.back() = record.final_origin_;
  record.initial_origin_ = record.redirect_origins_.front();

  // Chromium counts server redirects; the chain also grows for client
  // redirects, which are separate navigations. Keep the browser's own count
  // rather than deriving one from the chain length.
  record.server_redirect_count_ =
      chain.empty() ? 0u : static_cast<size_t>(chain.size() - 1);

  record.crossed_origin_during_redirect_ = false;
  for (const Origin& origin : record.redirect_origins_) {
    if (!(origin == record.final_origin_)) {
      record.crossed_origin_during_redirect_ = true;
      break;
    }
  }

  NavigationErrorFacts facts;
  facts.net_error = static_cast<int>(navigation_handle->GetNetErrorCode());
  facts.http_status_code = ResponseCodeFor(navigation_handle);
  facts.cert_status = CertStatusFor(navigation_handle);
  facts.is_error_page = navigation_handle->IsErrorPage();
  // blocked_by_safe_browsing and has_unrecognised_interstitial stay false
  // here: the security interstitial layer owns both signals and this file has
  // no honest way to read them. See NavigationErrorFacts for the SP-01 item
  // and for why false is the fail-closed answer.
  record.error_verdict_ = ClassifyNavigationError(facts);

  return record;
}

bool BrowserNavigationRecord::CarriesRequestedContent() const {
  return !error_verdict_.content_is_absent &&
         error_verdict_.interstitial == InterstitialKind::kNone;
}

}  // namespace taffy
