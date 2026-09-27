// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_TAFFY_PRODUCT_IDENTITY_H_
#define TAFFY_PUBLIC_TAFFY_PRODUCT_IDENTITY_H_

#include <string>

// The product identity seam for the M1 browser core.
//
// A Chromium derivative does not inherit Google-owned branding, the Chrome
// name and logo, or Google-only APIs and services: TaffyGo ships its own name,
// icons and about-page attribution (chromium/README.md, decision 0001). This
// header is the one place browser-process code asks "what product am I", so
// that the answer is not spelled out again in a dozen call sites.
//
// It is a seam, not a re-implementation. It does not compute a version, it
// does not build a user agent string, and it does not own the application id
// or the manifest: those belong to //taffy/app/android and to
// Chromium's own version_info, and restating any of them here would create a
// second source of truth for a value that already has one.

namespace taffy {

struct ProductIdentity {
  // The product name as it appears in browser UI and in the about page.
  std::string product_name;

  // The product token a user agent string would be built from. Empty, and
  // decision 0130 settles that it stays empty: the user agent string is frozen
  // upstream to remove fingerprinting entropy, and the product names itself in
  // the client-hint brand list instead. Nothing may splice this into a header.
  std::string user_agent_product_token;

  // False in a build that has not had TaffyGo branding applied, such as the
  // clean-host upstream baseline used to prove the toolchain. Code that would
  // present the product to a user checks this rather than assuming.
  bool is_taffy_branded = false;

  // Resource path of the complete third-party attribution set that ships with
  // the package. Chromium is BSD licensed with additional third-party
  // licenses and the fork inherits every one of those obligations.
  std::string attribution_resource_path;
};

// Never partially filled. Safe to call from any thread after browser-process
// startup. Defined in //taffy/browser: this directory declares
// interfaces and value types, and holds no definitions of its own.
const ProductIdentity& GetProductIdentity();

}  // namespace taffy

#endif  // TAFFY_PUBLIC_TAFFY_PRODUCT_IDENTITY_H_
