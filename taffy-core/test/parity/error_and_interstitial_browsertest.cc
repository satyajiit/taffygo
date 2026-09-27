// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/browser/navigation_error_classifier.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/navigation_handle_observer.h"
#include "content/shell/browser/shell.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

// Error pages, the captive portal, and the certificate interstitial
// (PAR-NAV-006 and PAR-NAV-007).
//
// The reason these need a browser rather than a classifier unit test is the
// same in every case: an error page is a committed navigation to a document the
// browser generated, and the failure mode worth catching is the one where the
// browser reports it as an ordinary page. A classifier fed the right error code
// always answers correctly; what a suite has to prove is that the right error
// code reaches it.
//
// Nothing here bypasses an upstream security check, and nothing adds an
// exception. PAR-SEC-001 forbids weakening a Chromium security feature without
// an approved decision record, and a test that clicked through an interstitial
// to make an assertion easier would be doing exactly that in miniature.

namespace taffy::test {
namespace {

constexpr char kUnresolvableHost[] = "does-not-resolve.taffy.test";

class ParityErrorPageTest : public TaffyBrowserTestBase {
 public:
  // The mock resolver maps everything to the loopback interface, so a name has
  // to be excluded from it to fail. This is the supported way to produce a
  // resolution failure in a browser test; simulating one would test the
  // simulation.
  //
  // It goes here rather than in the test body for two independent reasons,
  // both spelled out on TaffyBrowserTestBase::AddHostResolverRules(): the
  // wildcard rule the base installs would match first, and the resolver stops
  // accepting rules before a test body runs.
  void AddHostResolverRules() override {
    host_resolver()->AddSimulatedFailure(kUnresolvableHost);
  }

  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    NavigationLifecycleTracker::CreateForWebContents(web_contents());
    tracker_ = NavigationLifecycleTracker::FromWebContents(web_contents());
  }

 protected:
  NavigationLifecycleTracker* tracker() { return tracker_; }

  // A port on the loopback interface that nothing is listening on, obtained by
  // binding one and letting it go.
  //
  // A hard-coded low port does not work: Chromium refuses a navigation to a
  // restricted port with ERR_UNSAFE_PORT before a connection is ever
  // attempted, which is a policy refusal rather than a network failure and
  // classifies as kBlockedByClient. Asking the operating system for a port and
  // then releasing it produces the real ERR_CONNECTION_REFUSED this row is
  // about.
  uint16_t AClosedPort() {
    net::EmbeddedTestServer briefly_bound;
    if (!briefly_bound.Start()) {
      ADD_FAILURE() << "Could not bind a port to then release it.";
      return 0;
    }
    const uint16_t port = briefly_bound.port();
    if (!briefly_bound.ShutdownAndWaitUntilComplete()) {
      ADD_FAILURE() << "The briefly bound server did not shut down, so the "
                       "port is not actually closed.";
      return 0;
    }
    return port;
  }

 private:
  raw_ptr<NavigationLifecycleTracker> tracker_ = nullptr;
};

// PAR-NAV-006. A name that does not resolve produces an error page, not a
// blank document that looks like a page with no content. The distinction is
// what stops an assistant reporting an empty extraction as a complete one.
IN_PROC_BROWSER_TEST_F(ParityErrorPageTest, UnresolvableHostProducesAnErrorPage) {
  // The name excluded from the wildcard rule in AddHostResolverRules().
  const GURL url(base::StrCat(
      {"http://", kUnresolvableHost, "/article/anything.html"}));
  content::NavigationHandleObserver observer(web_contents(), url);
  EXPECT_FALSE(content::NavigateToURL(shell(), url));

  EXPECT_TRUE(observer.has_committed());
  EXPECT_TRUE(observer.is_error());
  EXPECT_NE(net::OK, observer.net_error_code());

  NavigationErrorFacts facts;
  facts.net_error = observer.net_error_code();
  facts.is_error_page = true;
  const NavigationErrorVerdict verdict = ClassifyNavigationError(facts);
  EXPECT_TRUE(verdict.content_is_absent)
      << "A page that did not load must be reported as content absent. This "
         "is the value a task runtime consults before recording that it read a "
         "page, and PAR-NAV-006 requires no false completion.";
  EXPECT_TRUE(NavigationErrorIsRetryable(verdict.error_class))
      << "A name that does not resolve is worth offering a retry for.";
}

// PAR-NAV-006. A connection refused on an origin that does resolve is a
// different error class from a name that does not resolve, and reporting both
// as "offline" would tell a user to check a network connection that is fine.
IN_PROC_BROWSER_TEST_F(ParityErrorPageTest, RefusedConnectionIsItsOwnErrorClass) {
  // A port nothing is listening on, on a host that resolves.
  const uint16_t closed_port = AClosedPort();
  ASSERT_NE(0u, closed_port);
  const GURL url = OriginUrl("primary", "/article/quantum-storage-review.html");
  GURL::Replacements replacement;
  const std::string port_string = base::NumberToString(closed_port);
  replacement.SetPortStr(port_string);
  const GURL refused = url.ReplaceComponents(replacement);

  content::NavigationHandleObserver observer(web_contents(), refused);
  EXPECT_FALSE(content::NavigateToURL(shell(), refused));
  EXPECT_TRUE(observer.is_error());
  EXPECT_EQ(net::ERR_CONNECTION_REFUSED, observer.net_error_code())
      << "The navigation did not fail the way this case is about. A refusal "
         "for any other reason classifies differently, so the assertion below "
         "would be about something else.";

  NavigationErrorFacts refused_facts;
  refused_facts.net_error = observer.net_error_code();
  refused_facts.is_error_page = true;
  const NavigationErrorVerdict verdict = ClassifyNavigationError(refused_facts);
  EXPECT_TRUE(verdict.content_is_absent);
  EXPECT_EQ(InterstitialKind::kNone, verdict.interstitial)
      << "A refused connection is a network failure, not an interstitial. "
         "Reporting one as the other would tell a user their connection is "
         "being intercepted when it is not.";
}

// PAR-NAV-006. The corpus's captive-portal probe redirects to the portal
// interstitial. A run that treated the portal page as the requested content
// would extract the portal's text and present it as the site's.
IN_PROC_BROWSER_TEST_F(ParityErrorPageTest, CaptivePortalProbeLandsOnThePortal) {
  const GURL probe = OriginUrl("primary", "/net/probe");
  const GURL portal = FixtureUrl("captive-portal");
  ASSERT_TRUE(content::NavigateToURL(shell(), probe, portal));
  EXPECT_EQ(portal, web_contents()->GetLastCommittedURL());
}

// PAR-NAV-006. The flaky endpoint fails twice and then succeeds, and each
// attempt is its own navigation. Retrying is the user's or the runtime's
// decision; what this proves is that the failures are visible as failures
// rather than as empty successes.
IN_PROC_BROWSER_TEST_F(ParityErrorPageTest, TransientFailureIsVisibleUntilItSucceeds) {
  for (int attempt = 1; attempt <= 2; ++attempt) {
    const GURL url = OriginUrl(
        "primary", "/net/flaky?attempt=" + base::NumberToString(attempt));
    ASSERT_TRUE(content::NavigateToURL(shell(), url));
    EXPECT_EQ(true, content::EvalJs(
                        web_contents(),
                        "document.body.innerText.includes('temporary failure')"))
        << "attempt " << attempt;
  }

  const GURL succeeded = OriginUrl("primary", "/net/flaky?attempt=3");
  ASSERT_TRUE(content::NavigateToURL(shell(), succeeded));
  EXPECT_EQ(true,
            content::EvalJs(web_contents(),
                            "document.body.innerText.includes('Stock level')"));
}

// PAR-NAV-007. A certificate the browser does not trust produces an
// interstitial, and the navigation does not deliver the site's content.
//
// The suite asserts the refusal and stops there. Proceeding past an
// interstitial is a published-policy decision, not a test convenience, and a
// test that clicked through would be weakening a Chromium security feature to
// make an assertion easier.
class ParityCertificateInterstitialTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    // A second server, separate from the corpus map, presenting a certificate
    // for a name it does not cover.
    //
    // VERIFY AT SP-01: net::EmbeddedTestServer::CERT_MISMATCHED_NAME at the
    // pinned milestone. Upstream file to read:
    // net/test/embedded_test_server/embedded_test_server.h. If the enumerator
    // moved, this is the only line that changes.
    bad_certificate_server_.SetSSLConfig(
        net::EmbeddedTestServer::CERT_MISMATCHED_NAME);
    bad_certificate_server_.ServeFilesFromSourceDirectory("content/test/data");
    ASSERT_TRUE(bad_certificate_server_.Start());
  }

 protected:
  net::EmbeddedTestServer bad_certificate_server_{
      net::EmbeddedTestServer::TYPE_HTTPS};
};

IN_PROC_BROWSER_TEST_F(ParityCertificateInterstitialTest,
                       MismatchedCertificateDoesNotDeliverContent) {
  const GURL url = bad_certificate_server_.GetURL("wrong.taffy.test", "/title1.html");
  content::NavigationHandleObserver observer(web_contents(), url);
  EXPECT_FALSE(content::NavigateToURL(shell(), url));

  EXPECT_TRUE(observer.is_error())
      << "A mismatched certificate must not deliver the site's document. If "
         "this passes as a successful navigation, an upstream security check "
         "was disabled somewhere in this build.";
  EXPECT_EQ(net::ERR_CERT_COMMON_NAME_INVALID, observer.net_error_code());
}

}  // namespace
}  // namespace taffy::test
