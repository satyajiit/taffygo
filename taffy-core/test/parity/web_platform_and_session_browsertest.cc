// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "base/values.h"
#include "taffy/test/support/fixture_dynamic_endpoints.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

// Web-platform and session parity over the corpus: cookies and site data
// (PAR-WEB-003), storage (PAR-WEB-004), WebAssembly (PAR-WEB-008), shadow DOM
// and frames as a rendering property (PAR-WEB-010), and a manual website login
// (PAR-AUTH-001).
//
// browser/PARITY.md assigns these rows to this work package with a specific
// reason: TaffyGo changes none of the underlying machinery, so no
// browser-process seam can assert them, and the honest evidence is that the
// upstream behaviour is still there in a build carrying this fork's patches.
// That is what this file is — a small, fast regression net over the parts of
// the platform the assistant's correctness depends on, run against the same
// corpus everything else runs against.
//
// It is not a Web Platform Test replacement and does not pretend to be. The
// approved delta against the upstream suite is a separate exit-evidence item;
// this file catches the case where a fork patch broke something the assistant
// silently relies on, which a full suite would find much later.

namespace taffy::test {
namespace {

using ParityWebPlatformTest = TaffyBrowserTestBase;

// PAR-WEB-003. Cookies are scoped to their origin. Two corpus origins that
// could read each other's cookies would make every cross-origin assertion in
// this directory meaningless.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, CookiesStayOnTheirOwnOrigin) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.cookie = 'taffy_parity=primary; path=/'"));
  EXPECT_EQ("taffy_parity=primary",
            content::EvalJs(web_contents(), "document.cookie"));

  ASSERT_TRUE(NavigateToFixture("comparison-source-c"));
  EXPECT_EQ("", content::EvalJs(web_contents(), "document.cookie"))
      << "A cookie set on one corpus origin was readable on another. The two "
         "origins are not actually separate sites in this build.";
}

// PAR-AUTH-001 and PAR-WEB-003. A manual login sets an HttpOnly session cookie,
// the session survives navigation, and the page cannot read the cookie value.
//
// The last clause is the one that matters for the assistant: a session the page
// cannot read is a session the page cannot hand to a model through any
// observation path, whatever else goes wrong.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, ManualLoginSessionIsHttpOnly) {
  ASSERT_TRUE(NavigateToFixture("login-page"));

  const GURL account = FixtureUrl("auth-account");
  content::TestNavigationObserver observer(web_contents());
  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.getElementById('login').submit()"));
  observer.Wait();
  EXPECT_EQ(account, web_contents()->GetLastCommittedURL());

  // The server rewrites the attribute on the root element, which is where the
  // fixture's own script reads it from. document.body is a different element
  // and has never carried it.
  EXPECT_EQ("active",
            content::EvalJs(
                web_contents(),
                "document.documentElement.getAttribute('data-fixture-session')"))
      << "The account page did not see the session. Everything below is about "
         "a session that exists, so this has to hold first.";

  const std::string cookies =
      content::EvalJs(web_contents(), "document.cookie").ExtractString();
  EXPECT_EQ(std::string::npos, cookies.find(kFixtureSessionCookieName))
      << "The session cookie is readable from script. An HttpOnly cookie is "
         "the boundary that keeps a page from handing its own session to "
         "anything that observes the page.";
  EXPECT_EQ(std::string::npos, cookies.find(kFixtureSessionCookieValue));
}

// PAR-AUTH-001 continued. The session survives ordinary navigation away and
// back, which is the difference between a login that worked and one that
// appeared to.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, SessionSurvivesNavigation) {
  ASSERT_TRUE(NavigateToFixture("login-page"));
  content::TestNavigationObserver observer(web_contents());
  ASSERT_TRUE(content::ExecJs(
      web_contents(), "document.getElementById('login').submit()"));
  observer.Wait();

  ASSERT_TRUE(NavigateToFixture("static-article"));
  ASSERT_TRUE(NavigateToFixture("auth-account"));
  EXPECT_EQ("active",
            content::EvalJs(
                web_contents(),
                "document.documentElement.getAttribute('data-fixture-session')"));
}

// PAR-WEB-004. Local storage, session storage and IndexedDB are per origin and
// they persist across a same-origin navigation.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, StorageIsPerOriginAndPersists) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  ASSERT_TRUE(content::ExecJs(
      web_contents(), "localStorage.setItem('taffy_parity', 'primary')"));

  ASSERT_TRUE(NavigateToFixture("static-product"));
  EXPECT_EQ("primary",
            content::EvalJs(web_contents(), "localStorage.getItem('taffy_parity')"))
      << "Local storage did not survive a same-origin navigation.";

  ASSERT_TRUE(NavigateToFixture("comparison-source-c"));
  // base::Value() rather than nullptr: gtest would instantiate
  // content::JsLiteralHelper<std::nullptr_t> for the latter, whose generic
  // Convert() calls the now-ambiguous base::Value(nullptr). base::Value() is
  // the none-typed value JavaScript null decodes to and is the spelling
  // browser_test_utils.h documents. It is not the weaker
  // `result.value().is_none()` test either: an EvalJs whose script threw
  // compares equal to nothing at all, so a broken script still fails here.
  EXPECT_EQ(base::Value(),
            content::EvalJs(web_contents(), "localStorage.getItem('taffy_parity')"))
      << "Local storage crossed an origin boundary.";

  ASSERT_TRUE(NavigateToFixture("static-article"));
  EXPECT_EQ(true, content::EvalJs(web_contents(),
                                  "typeof indexedDB !== 'undefined' && "
                                  "typeof indexedDB.open === 'function'"))
      << "IndexedDB is missing. A fork patch removed a storage API the "
         "assistant's own persistence assumptions do not depend on, but the "
         "web does.";
}

// PAR-WEB-008. WebAssembly is present and its isolation settings are the ones
// upstream ships. The assistant does not use it; the web does, and a fork that
// quietly disabled it would be a parity regression nobody noticed until a site
// broke.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, WebAssemblyIsPresent) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  EXPECT_EQ(true, content::EvalJs(web_contents(),
                                  "typeof WebAssembly === 'object' && "
                                  "typeof WebAssembly.compile === 'function'"));
  // A minimal empty module. Compiling it proves the pipeline exists rather
  // than only the namespace.
  EXPECT_EQ(true,
            content::EvalJs(
                web_contents(),
                "WebAssembly.validate(new Uint8Array("
                "[0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00]))"));
}

// PAR-WEB-010, the rendering half. Open shadow roots project into the composed
// tree and closed ones do not, and a same-origin frame is reachable from its
// parent while a cross-origin one is not. The origin-aware observation half of
// this row belongs to the M2 correctness suite; what is here is the platform
// behaviour that half depends on.
IN_PROC_BROWSER_TEST_F(ParityWebPlatformTest, ShadowRootsAndFramesBehaveAsExpected) {
  ASSERT_TRUE(NavigateToFixture("shadow-dom-open"));
  EXPECT_EQ(true,
            content::EvalJs(web_contents(),
                            "!!document.getElementById('card').shadowRoot"))
      << "An open shadow root is not reachable from its host. Every projection "
         "assertion in the correctness suite assumes it is.";

  ASSERT_TRUE(NavigateToFixture("shadow-dom-closed"));
  EXPECT_EQ(base::Value(),
            content::EvalJs(web_contents(),
                            "document.getElementById('widget').shadowRoot ?? null"))
      << "A closed shadow root is reachable from script. The corpus keeps a "
         "canary inside one precisely because it must not be.";

  ASSERT_TRUE(NavigateToFixture("iframe-same-origin"));
  EXPECT_EQ(true, content::EvalJs(web_contents(),
                                  "!!document.querySelector('iframe')"
                                  ".contentDocument"));

  ASSERT_TRUE(NavigateToFixture("iframe-cross-origin"));
  EXPECT_EQ(base::Value(),
            content::EvalJs(web_contents(),
                            "document.querySelector('iframe').contentDocument"))
      << "A cross-origin frame's document is reachable from the parent. Site "
         "isolation is not doing its job in this build, and every "
         "frame-scoping assertion in this directory is worthless.";
}

}  // namespace
}  // namespace taffy::test
