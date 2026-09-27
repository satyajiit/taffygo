// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/taffy_browser_test_main_delegate.h"

#include "taffy/test/taffy_content_renderer_client.h"
#include "taffy/test/taffy_content_utility_client.h"

namespace taffy::test {

TaffyBrowserTestMainDelegate::TaffyBrowserTestMainDelegate() = default;

TaffyBrowserTestMainDelegate::~TaffyBrowserTestMainDelegate() = default;

content::ContentRendererClient*
TaffyBrowserTestMainDelegate::CreateContentRendererClient() {
  // `true` is upstream's `is_browsertest`: it is what ShellContentRendererClient
  // reads to keep the behaviours a browser test needs and drop the ones only an
  // interactive shell wants. ContentBrowserTestShellMainDelegate passes the same
  // value to its browser client, so this keeps the two halves consistent.
  renderer_client_ = std::make_unique<TaffyContentRendererClient>(
      /*is_browsertest=*/true);
  return renderer_client_.get();
}

content::ContentUtilityClient*
TaffyBrowserTestMainDelegate::CreateContentUtilityClient() {
  // The product's own registry, so a worker this binary launches is the worker
  // the product launches. Upstream's ShellContentUtilityClient registers
  // content_shell's services and knows nothing about //taffy; a suite that
  // needs a TaffyGo service in a real sandboxed process needs this.
  utility_client_ = std::make_unique<TaffyContentUtilityClient>();
  return utility_client_.get();
}

}  // namespace taffy::test
