// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/credential_boundary.h"

#include <string>

#include "taffy/browser/oauth_continuity_tracker.h"
#include "taffy/browser/oauth_return_facts.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

// PAR-AUTH-001 (a manual website login works and its session persists) and
// PAR-AUTH-004 (an external authorization app switch returns to the same tab
// without leaking callback data), against a real WebContents.
//
// The seeded value below is the one the fixture corpus uses for the login page
// (test-fixtures/web/manifest.json, canary TAFFYGO-CANARY-LOGIN-0A3F7D). It is
// typed into a real password field here so that the assertion "no TaffyGo path
// carries it" is made against a value that genuinely passed through the
// browser, rather than against a value that was never entered.

namespace taffy {
namespace {

constexpr char kSeededLoginSecret[] = "TAFFYGO-CANARY-LOGIN-0A3F7D";

class CredentialIsolationBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    tracker_ = std::make_unique<OAuthContinuityTracker>(&boundary_);
  }

  void TearDownOnMainThread() override {
    tracker_.reset();
    OriginCodec::Get().ClearForTesting();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  Origin WireOrigin(const GURL& url) {
    return OriginCodec::Get().ToWireOrigin(url::Origin::Create(url));
  }

  content::WebContents* web_contents() { return shell()->web_contents(); }

  CredentialBoundary boundary_;
  std::unique_ptr<OAuthContinuityTracker> tracker_;
};

IN_PROC_BROWSER_TEST_F(CredentialIsolationBrowserTest,
                       ManualLoginWorksAndItsSessionPersists) {
  // The login itself: a cookie the server sets, which is what a real site
  // session is made of. It must survive a navigation with nothing from the
  // assistant involved.
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/set-cookie?session=granted")));
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/echoheader?Cookie")));

  EXPECT_EQ("session=granted",
            content::EvalJs(web_contents(), "document.body.innerText.trim()"));
}

IN_PROC_BROWSER_TEST_F(CredentialIsolationBrowserTest,
                       AssistantAccessIsSuspendedWhilePasswordIsEntered) {
  const TabId tab{"tab_login"};
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));

  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "const f = document.createElement('input');"
                              "f.type = 'password';"
                              "f.id = 'password';"
                              "document.body.appendChild(f);"));

  CredentialFieldMetadata metadata;
  metadata.tab_id = ToRecordIdentifier("tab_login");
  metadata.frame_id = ToRecordIdentifier("frame_main");
  metadata.node_id = ToRecordIdentifier("node_password");
  metadata.credential_class = CredentialClass::kPassword;
  boundary_.NoteCredentialField(metadata);

  EXPECT_TRUE(boundary_.AssistantMayObserve(tab));

  boundary_.BeginCredentialInteraction(tab, CredentialClass::kPassword);
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      std::string("document.getElementById('password').value = '") +
          kSeededLoginSecret + "';"));

  // The value is now in a real password field in a real renderer. For as long
  // as the interaction lasts, no TaffyGo path may look at this tab at all —
  // not a redacted observation, not a field summary, not a count of
  // keystrokes.
  EXPECT_EQ(AssistantAccess::kSuspendedCredentialInteraction,
            boundary_.EvaluateAssistantAccess(tab));
  EXPECT_FALSE(boundary_.AssistantMayObserve(tab));
  EXPECT_FALSE(boundary_.AssistantMayAct(tab));

  boundary_.EndCredentialInteraction(tab, CredentialClass::kPassword);
  EXPECT_TRUE(boundary_.AssistantMayObserve(tab));

  // And the browser-process record of the field still says nothing about it.
  EXPECT_EQ(1u, boundary_.CredentialFieldCount(tab));
}

IN_PROC_BROWSER_TEST_F(CredentialIsolationBrowserTest,
                       AppSwitchReturnsToTheSameTabWithoutCallbackData) {
  const TabId tab{"tab_oauth"};
  const GURL site = PageUrl("primary.test", "/title1.html");
  const GURL provider = PageUrl("provider.test", "/title2.html");
  const GURL callback =
      PageUrl("primary.test", "/auth/callback?code=SECRET&state=OPAQUE");

  ASSERT_TRUE(content::NavigateToURL(shell(), site));

  OAuthHandoffRequest request;
  request.tab_id = tab;
  request.initiating_origin = WireOrigin(site);
  request.expected_callback_origin = WireOrigin(site);
  request.expected_callback_path = "/auth/callback";
  request.initiator = NavigationInitiator::kUser;
  request.has_user_activation = true;
  ASSERT_EQ(OAuthHandoffResult::kRecorded, tracker_->BeginAppSwitch(request));

  // The user is at the provider. The tab is unobservable for the whole trip.
  ASSERT_TRUE(content::NavigateToURL(shell(), provider));
  EXPECT_EQ(AssistantAccess::kSuspendedAuthorizationHandoff,
            boundary_.EvaluateAssistantAccess(tab));

  // The callback commits. It carries a code and a state parameter, and the
  // facts built from it carry neither, because there is nowhere in the type to
  // put them.
  ASSERT_FALSE(content::NavigateToURL(shell(), callback));
  const OAuthReturnFacts facts =
      OAuthReturnFacts::FromCallbackUrl(callback, tab);
  EXPECT_TRUE(facts.carried_callback_parameters());
  EXPECT_EQ("/auth/callback", facts.callback_path());

  EXPECT_EQ(OAuthReturnDecision::kResumeWaitingTab,
            tracker_->CompleteAppSwitch(facts));
  EXPECT_EQ(AssistantAccess::kPermitted,
            boundary_.EvaluateAssistantAccess(tab));
}

IN_PROC_BROWSER_TEST_F(CredentialIsolationBrowserTest,
                       ACallbackFromAnotherOriginDoesNotResume) {
  const TabId tab{"tab_oauth"};
  const GURL site = PageUrl("primary.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(shell(), site));

  OAuthHandoffRequest request;
  request.tab_id = tab;
  request.initiating_origin = WireOrigin(site);
  request.expected_callback_origin = WireOrigin(site);
  request.expected_callback_path = "/auth/callback";
  request.initiator = NavigationInitiator::kUser;
  request.has_user_activation = true;
  ASSERT_EQ(OAuthHandoffResult::kRecorded, tracker_->BeginAppSwitch(request));

  const GURL hostile_callback =
      PageUrl("hostile.test", "/auth/callback?code=SECRET");
  EXPECT_EQ(OAuthReturnDecision::kRefusedOriginMismatch,
            tracker_->CompleteAppSwitch(
                OAuthReturnFacts::FromCallbackUrl(hostile_callback, tab)));
}

}  // namespace
}  // namespace taffy
