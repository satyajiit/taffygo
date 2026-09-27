// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/test/test_future.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "taffy/browser/taffy_browser_effect_source.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {

// Opens the exact browser-effect watermark ActionDispatcher opens after a
// capability and journal append have succeeded. The policy hook deliberately
// sees only whether that window is open, so this peer exposes no task, URL, or
// fabricated authorization to the test.
class DownloadAuthorityTestPeer final {
 public:
  static DispatchWatermark Open(TaffyPageIntelligenceHost& host) {
    return host.browser_effects_->NoteDispatch();
  }

  static void Close(TaffyPageIntelligenceHost& host,
                    DispatchWatermark watermark) {
    host.browser_effects_->CloseDispatch(watermark);
  }
};

namespace {

class DownloadAuthorityVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    PlatformBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(content::NavigateToURL(
        active_tab(), embedded_test_server()->GetURL("/title1.html")));
  }

  content::WebContents* active_tab() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  bool CheckDownloadAllowed(bool content_initiated) {
    content::WebContents* const tab = active_tab();
    CHECK(tab);
    content::DownloadManagerDelegate* const delegate =
        tab->GetBrowserContext()->GetDownloadManagerDelegate();
    CHECK(delegate);

    base::test::TestFuture<bool> allowed;
    delegate->CheckDownloadAllowed(
        base::BindRepeating(
            [](base::WeakPtr<content::WebContents> contents) {
              return contents.get();
            },
            tab->GetWeakPtr()),
        embedded_test_server()->GetURL("/download.bin"), "GET", std::nullopt,
        /*from_download_cross_origin_redirect=*/false, content_initiated,
        "application/octet-stream", std::nullopt, allowed.GetCallback());
    return allowed.Get();
  }
};

IN_PROC_BROWSER_TEST_F(
    DownloadAuthorityVerticalBrowserTest,
    ContentInitiatedDownloadIsRefusedOnlyInsideAssistantDispatch) {
  content::WebContents* const tab = active_tab();
  ASSERT_TRUE(tab);
  TaffyPageIntelligenceHost* const host =
      TaffyPageIntelligenceHost::FromWebContents(tab);
  ASSERT_TRUE(host);

  const DispatchWatermark watermark = DownloadAuthorityTestPeer::Open(*host);
  ASSERT_NE(0u, watermark.value);
  ASSERT_TRUE(host->HasOpenAssistantDispatch());
  EXPECT_FALSE(CheckDownloadAllowed(/*content_initiated=*/true));

  DownloadAuthorityTestPeer::Close(*host, watermark);
  ASSERT_FALSE(host->HasOpenAssistantDispatch());
  EXPECT_TRUE(CheckDownloadAllowed(/*content_initiated=*/true));
}

IN_PROC_BROWSER_TEST_F(DownloadAuthorityVerticalBrowserTest,
                       NonContentInitiatedDownloadIsNotRefusedByThisGate) {
  content::WebContents* const tab = active_tab();
  ASSERT_TRUE(tab);
  TaffyPageIntelligenceHost* const host =
      TaffyPageIntelligenceHost::FromWebContents(tab);
  ASSERT_TRUE(host);

  const DispatchWatermark watermark = DownloadAuthorityTestPeer::Open(*host);
  ASSERT_NE(0u, watermark.value);
  ASSERT_TRUE(host->HasOpenAssistantDispatch());
  // Dedicated StartDownload calls arrive with this shape only after their
  // separate task/capability/journal admission. Human browser operations may
  // also be non-content-initiated; this hook must not claim either authority.
  EXPECT_TRUE(CheckDownloadAllowed(/*content_initiated=*/false));
  DownloadAuthorityTestPeer::Close(*host, watermark);
}

}  // namespace
}  // namespace taffy
