// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_RESOLVER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_RESOLVER_H_

#include <optional>
#include <string>

#include "taffy/common/public/bip_identity.h"

namespace taffy {

// Trusted-browser authority consulted immediately before an observed-link
// navigation. Implementations are transient and per WebContents; a restart or
// page invalidation therefore answers null rather than reconstructing state.
class ObservedLinkResolver {
 public:
  virtual ~ObservedLinkResolver() = default;
  virtual std::optional<std::string> ResolveObservedLink(
      const NodeHandle& handle) const = 0;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_OBSERVED_LINK_RESOLVER_H_
