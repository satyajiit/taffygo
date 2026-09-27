// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_TAFFY_BROWSER_TEST_MAIN_DELEGATE_H_
#define TAFFY_TEST_TAFFY_BROWSER_TEST_MAIN_DELEGATE_H_

#include <memory>

#include "content/public/test/content_browser_test_shell_main_delegate.h"

namespace content {
class ContentRendererClient;
class ContentUtilityClient;
}  // namespace content

// The main delegate of `taffy_browsertests`.
//
// Upstream's ContentBrowserTestShellMainDelegate, plus a renderer client that
// creates TaffyGo's render frame observer. Everything else — the content
// client, the browser client, the thread pool, the browser-test behaviour — is
// upstream's and is inherited rather than reimplemented, because none of it is
// what this binary needs to be different about.
//
// Two overrides, and the reason they are the whole file: a delegate is the only
// place a downstream binary may install a ContentRendererClient or a
// ContentUtilityClient. Each is created in the process it names — not in the
// browser — so nothing a test does at run time can reach either. That is why an
// endpoint being bound is a property of the binary, and why the correctness
// suite refuses to run rather than pretend, when it is not; and it is why a
// suite that launches a sandboxed TaffyGo worker needs the utility client here
// rather than a registration a test could perform for itself.
namespace taffy::test {

class TaffyBrowserTestMainDelegate
    : public content::ContentBrowserTestShellMainDelegate {
 public:
  TaffyBrowserTestMainDelegate();
  TaffyBrowserTestMainDelegate(const TaffyBrowserTestMainDelegate&) = delete;
  TaffyBrowserTestMainDelegate& operator=(const TaffyBrowserTestMainDelegate&) =
      delete;
  ~TaffyBrowserTestMainDelegate() override;

  // content::ShellMainDelegate:
  content::ContentRendererClient* CreateContentRendererClient() override;
  content::ContentUtilityClient* CreateContentUtilityClient() override;

 private:
  // Held here rather than in ShellMainDelegate's own renderer_client_, which is
  // private to it and typed as ShellContentRendererClient.
  std::unique_ptr<content::ContentRendererClient> renderer_client_;
  std::unique_ptr<content::ContentUtilityClient> utility_client_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_TAFFY_BROWSER_TEST_MAIN_DELEGATE_H_
