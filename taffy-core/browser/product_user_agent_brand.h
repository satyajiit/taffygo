// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PRODUCT_USER_AGENT_BRAND_H_
#define TAFFY_BROWSER_PRODUCT_USER_AGENT_BRAND_H_

#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

// The product's own entry in the user agent client-hint brand list.
//
// Decision
// //docs/decisions/0130-the-product-names-itself-in-the-brand-list-and-not-in-the-user-agent-string.md
// settles where a Chromium derivative may name itself: in the brand list, and
// never in the user agent string, which upstream has deliberately frozen to
// the same bytes for every derivative. This is the whole of the brand-list
// half, kept here so that each upstream hook calling it is a few lines and
// holds no string of ours.
//
// There are two such hooks and there have to be, because the brand list reaches
// a page and a header by different routes: ContentBrowserClient for the
// renderer and the worker hosts, and a ClientHintsControllerDelegate for
// Sec-CH-UA. A build that called this from only one of them would brand
// navigator.userAgentData and leave the header unbranded, which is a
// fingerprinting signal rather than a name.
namespace taffy {

// Appends the product to `metadata`'s brand lists, in place.
//
// Appends nothing when the build carries no TaffyGo branding, so the clean-host
// upstream baseline still reports itself as upstream; and nothing on a second
// call for the same metadata, because the caller is an embedder hook and an
// embedder hook that is invoked twice must not name the product twice.
//
// The full-version list is extended only when it is already populated.
// `embedder_support::GetUserAgentMetadata` leaves it empty when the caller
// asked for low-entropy hints alone, and filling it here would hand that caller
// the high-entropy hint it declined.
void AddProductBrand(blink::UserAgentMetadata& metadata);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PRODUCT_USER_AGENT_BRAND_H_
