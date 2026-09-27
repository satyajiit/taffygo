// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/oauth_return_facts.h"

#include "taffy/components/intelligence/content/origin_codec.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

OAuthReturnFacts::OAuthReturnFacts() = default;
OAuthReturnFacts::OAuthReturnFacts(const OAuthReturnFacts&) = default;
OAuthReturnFacts& OAuthReturnFacts::operator=(const OAuthReturnFacts&) =
    default;
OAuthReturnFacts::~OAuthReturnFacts() = default;

// static
OAuthReturnFacts OAuthReturnFacts::FromCallbackUrl(const GURL& callback_url,
                                                   const TabId& tab_id) {
  OAuthReturnFacts facts;
  facts.tab_id_ = tab_id;

  // The whole design of this file is these four lines. The query and the
  // fragment are read once, to answer "was there anything there", and are then
  // out of scope. Nothing assigns them anywhere, because nothing could: this
  // class has no member that would hold them.
  facts.carried_callback_parameters_ =
      callback_url.has_query() || callback_url.has_ref();

  facts.callback_origin_ =
      OriginCodec::Get().ToWireOrigin(url::Origin::Create(callback_url));
  facts.callback_path_ = callback_url.path();

  return facts;
}

}  // namespace taffy
