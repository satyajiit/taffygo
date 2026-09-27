// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.taffygo.browser.ui.core.browser.NavigationState;
import com.taffygo.browser.ui.core.model.PageLoadFailure;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.net.NetError;

import java.util.EnumSet;
import java.util.Set;

/**
 * The words a person is shown when a page fails, checked on a laptop.
 *
 * <p>This is the half of {@code ChromiumBrowserMediator} that has no browser in it, and it is
 * separated precisely so this suite can exist: driving these cases through a real engine would mean
 * making a device fail a DNS lookup, time out a connection and serve an expired certificate on
 * demand, and a mapping that is only checked that way is a mapping that is not checked.
 *
 * <p>Nothing under test touches Android or a native library — the runner below is this target's
 * own, shared with {@code TaffyBrowserActivityWindowTest}, and it runs on the host JVM. Every error
 * name is a compile-time constant from {@code net/base/net_error_list.h} by way of
 * {@code NetError}, so a code upstream renumbers fails this suite to <i>compile</i> rather than
 * passing it with the wrong answer.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyNavigationProjectionTest {

    /** Convenience: the mapping under test, with the parts this suite does not vary held still. */
    private static PageLoadFailure failureFor(int netErrorCode) {
        return TaffyNavigationProjection.of(
                        "https://example.test/",
                        "Example",
                        /* canGoBack= */ false,
                        /* canGoForward= */ false,
                        /* isLoading= */ false,
                        netErrorCode,
                        /* filteringActive= */ false,
                        /* siteExcepted= */ false,
                        /* blockedRequestCount= */ 0)
                .getFailure();
    }

    // -----------------------------------------------------------------------
    // The failure mapping, code by code.
    // -----------------------------------------------------------------------

    @Test
    public void aNavigationThatDidNotFailCarriesNoFailure() {
        assertNull(failureFor(NetError.OK));
    }

    @Test
    public void anAbortedNavigationIsNotAFailure() {
        // ERR_ABORTED is what the engine reports when one navigation replaces another, which
        // happens every time somebody types a second address before the first arrives. Treating it
        // as a failure would put an error notice on top of the page that succeeded.
        assertNull(failureFor(NetError.ERR_ABORTED));
    }

    @Test
    public void noNetworkReadsAsOffline() {
        assertEquals(PageLoadFailure.OFFLINE, failureFor(NetError.ERR_INTERNET_DISCONNECTED));
    }

    @Test
    public void bothWaysAHostNameCanFailReadAsNameNotResolved() {
        assertEquals(
                PageLoadFailure.NAME_NOT_RESOLVED, failureFor(NetError.ERR_NAME_NOT_RESOLVED));
        assertEquals(
                PageLoadFailure.NAME_NOT_RESOLVED, failureFor(NetError.ERR_NAME_RESOLUTION_FAILED));
    }

    @Test
    public void bothWaysAConnectionCanRunOutOfTimeReadAsTimedOut() {
        assertEquals(PageLoadFailure.TIMED_OUT, failureFor(NetError.ERR_TIMED_OUT));
        assertEquals(PageLoadFailure.TIMED_OUT, failureFor(NetError.ERR_CONNECTION_TIMED_OUT));
    }

    @Test
    public void everyCodeInTheCertificateRangeReadsAsCertificateInvalid() {
        // A range and not a list, because net::IsCertificateError is a range: upstream adds new
        // certificate failures inside it, and a list would silently stop covering the next one.
        // Walking every code in the block is what makes that claim rather than sampling it.
        for (int code = NetError.ERR_CERT_COMMON_NAME_INVALID;
                code > NetError.ERR_CERT_END;
                code--) {
            assertEquals(
                    "net error " + code,
                    PageLoadFailure.CERTIFICATE_INVALID,
                    failureFor(code));
        }
    }

    @Test
    public void aPinnedKeyFailureReadsAsCertificateInvalid() {
        // Upstream's one exception to the range above, and it is an exception in net's own
        // predicate rather than in this mapping.
        assertEquals(
                PageLoadFailure.CERTIFICATE_INVALID,
                failureFor(NetError.ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN));
    }

    @Test
    public void theCertificateRangeStopsWhereNetSaysItStops() {
        // The two codes either side of the block. Without these the range test above would still
        // pass if the mapping claimed every negative number was a certificate problem.
        assertNull("ERR_CERT_END is the exclusive end", failureFor(NetError.ERR_CERT_END));
        assertNull(
                "one code above the block start is not a certificate error",
                failureFor(NetError.ERR_CERT_COMMON_NAME_INVALID + 1));
    }

    @Test
    public void aCrashedRendererReadsAsPageCrashed() {
        // Not a network error: the mediator writes TaffyNavigationProjection.PAGE_CRASHED when
        // the renderer dies. The walk below never reaches Integer.MIN_VALUE, so this is the
        // assertion that the fifth set of words is reachable at all.
        assertEquals(
                PageLoadFailure.PAGE_CRASHED, failureFor(TaffyNavigationProjection.PAGE_CRASHED));
    }

    @Test
    public void aConnectionThatCannotBeReachedReadsAsUnreachable() {
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_CONNECTION_REFUSED));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_CONNECTION_RESET));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_CONNECTION_CLOSED));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_CONNECTION_FAILED));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_ADDRESS_UNREACHABLE));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_EMPTY_RESPONSE));
        assertEquals(PageLoadFailure.UNREACHABLE, failureFor(NetError.ERR_FAILED));
    }

    @Test
    public void everyDnsFailureReadsAsNameNotResolved() {
        assertEquals(PageLoadFailure.NAME_NOT_RESOLVED, failureFor(NetError.ERR_DNS_TIMED_OUT));
        assertEquals(
                PageLoadFailure.NAME_NOT_RESOLVED, failureFor(NetError.ERR_DNS_MALFORMED_RESPONSE));
        assertEquals(PageLoadFailure.NAME_NOT_RESOLVED, failureFor(NetError.ERR_DNS_SERVER_FAILURE));
    }

    @Test
    public void aFailureTaffyGoHasNoWordsForCarriesNoFailure() {
        // Stated rather than hidden: PageLoadFailure has six values because screen SCR-108 has
        // six sets of words, and the network stack has hundreds of codes. For anything outside
        // those six, TaffyGo shows no notice of its own and what a person sees is the engine's own
        // error page inside the page surface.
        assertNull(failureFor(NetError.ERR_BLOCKED_BY_CLIENT));
        assertNull(failureFor(NetError.ERR_BLOCKED_BY_RESPONSE));
    }

    @Test
    public void theEngineErrorDocumentIsRecognisedAndDoesNotClearARememberedError() {
        assertTrue(TaffyNavigationProjection.isErrorDocument("chrome-error://chromewebdata/"));
        assertTrue(TaffyNavigationProjection.isErrorDocument("chrome://network-error/-105"));
        assertFalse(TaffyNavigationProjection.isErrorDocument("https://example.test/"));
        assertFalse(TaffyNavigationProjection.clearsRememberedError("chrome-error://chromewebdata/"));
        assertTrue(TaffyNavigationProjection.clearsRememberedError("https://example.test/"));
    }

    @Test
    public void theEnginesOwnPagesAreRefusedAndOrdinaryAddressesAreNot() {
        // Every spelling of Chromium's own surfaces, including the ones the fixup produces from
        // an about: address, and the two the engine names rather than a person.
        String[] refused = {
            "chrome://settings/",
            "chrome://flags",
            // About opens the notices without a string (decision 0206); typing the address
            // still reaches this refusal.
            "chrome://credits",
            "CHROME://VERSION",
            "chrome://net-internals/#dns",
            "chrome-untrusted://feed/",
            "chrome-native://newtab/",
            "chrome-search://local-ntp/",
            "chrome-distiller://abc/",
            "devtools://devtools/bundled/inspector.html",
            "about:version",
        };
        for (String url : refused) {
            assertTrue(url, TaffyNavigationProjection.isEngineInternalPage(url));
        }

        // An empty tab is TaffyGo's own, and a site whose name merely begins with one of those
        // words is a site. The scheme is matched with its colon so it cannot be read off a host.
        String[] allowed = {
            TaffyNavigationProjection.BLANK_PAGE,
            "https://chrome.example.test/",
            "https://example.test/chrome://settings",
            "http://devtools.example.test/",
            "",
        };
        for (String url : allowed) {
            assertFalse(url, TaffyNavigationProjection.isEngineInternalPage(url));
        }
    }

    @Test
    public void anAbortDoesNotForgetANamedFailure() {
        int kept =
                TaffyNavigationProjection.rememberError(
                        NetError.ERR_NAME_NOT_RESOLVED, NetError.ERR_ABORTED);
        assertEquals(NetError.ERR_NAME_NOT_RESOLVED, kept);
        assertEquals(
                NetError.ERR_NAME_NOT_RESOLVED,
                TaffyNavigationProjection.rememberError(
                        NetError.ERR_NAME_NOT_RESOLVED, NetError.ERR_FAILED));
        assertEquals(
                TaffyNavigationProjection.PAGE_CRASHED,
                TaffyNavigationProjection.rememberError(
                        TaffyNavigationProjection.PAGE_CRASHED, NetError.ERR_FAILED));
    }

    @Test
    public void everyFailureTheProductCanShowIsReachableFromSomeNetError() {
        // The guard on the enum growing. A value added to PageLoadFailure that nothing maps to is
        // a set of words no page can ever produce, and this is the only place that would notice.
        Set<PageLoadFailure> produced = EnumSet.noneOf(PageLoadFailure.class);
        for (int code = 0; code > -1000; code--) {
            PageLoadFailure failure = failureFor(code);
            if (failure != null) produced.add(failure);
        }
        produced.add(failureFor(TaffyNavigationProjection.PAGE_CRASHED));
        assertEquals(EnumSet.allOf(PageLoadFailure.class), produced);
    }

    // -----------------------------------------------------------------------
    // The rest of the projection.
    // -----------------------------------------------------------------------

    @Test
    public void theHostIsWhatTheAddressPillShows() {
        assertEquals("example.test", hostFor("https://example.test/some/path?q=1#frag"));
        assertEquals("example.test", hostFor("http://example.test"));
        assertEquals("sub.example.test", hostFor("https://sub.example.test/"));
    }

    @Test
    public void theHostDropsThePort() {
        assertEquals("example.test", hostFor("https://example.test:8443/"));
    }

    @Test
    public void anIpv6LiteralKeepsItsBracketsAndLosesOnlyThePort() {
        assertEquals("[::1]", hostFor("http://[::1]:8080/"));
        assertEquals("[::1]", hostFor("http://[::1]/"));
    }

    @Test
    public void theHostDropsUserinfo() {
        // The oldest address-bar spoof there is: a page on evil.test that reads as apple.com if the
        // part before the @ is shown. This is the assertion that stops it.
        assertEquals("evil.test", hostFor("https://apple.com@evil.test/"));
        assertEquals("evil.test", hostFor("https://apple.com:secret@evil.test:8443/"));
    }

    @Test
    public void anAddressWithNoHostShowsNoHost() {
        assertEquals("", hostFor("about:blank"));
        assertEquals("", hostFor("data:text/html,hello"));
        assertEquals("", hostFor(""));
    }

    @Test
    public void pageActionsKeepTheExactCanonicalPathQueryAndFragment() {
        String address = "https://example.test/some/path?q=one%20two#frag";

        assertEquals(address, canonicalUrlFor(address));
        assertEquals("http://example.test:8080/path?mode=full#part", canonicalUrlFor(
                "http://example.test:8080/path?mode=full#part"));
    }

    @Test
    public void pageActionsRefuseNonWebAndUserinfoAddresses() {
        assertEquals("", canonicalUrlFor("about:blank"));
        assertEquals("", canonicalUrlFor("data:text/html,hello"));
        assertEquals("", canonicalUrlFor("file:///tmp/page.html"));
        assertEquals("", canonicalUrlFor("https://person:secret@example.test/path"));
        assertEquals("", canonicalUrlFor("https:///missing-host"));
    }

    // -----------------------------------------------------------------------
    // The tab that has been nowhere. `about:blank` is an internal address, and a person never
    // hears it — see docs/voice-and-naming.md.
    // -----------------------------------------------------------------------

    @Test
    public void theBlankPageAndTheEmptySpecBothMeanTheTabHasBeenNowhere() {
        assertTrue(TaffyNavigationProjection.hasBeenNowhere(TaffyNavigationProjection.BLANK_PAGE));
        assertTrue(TaffyNavigationProjection.hasBeenNowhere("about:blank"));
        // Before the first navigation commits, a tab reports no address at all.
        assertTrue(TaffyNavigationProjection.hasBeenNowhere(""));
    }

    @Test
    public void aPageThatHasLoadedHasBeenSomewhere() {
        assertFalse(TaffyNavigationProjection.hasBeenNowhere("https://example.test/"));
        assertFalse(TaffyNavigationProjection.hasBeenNowhere("data:text/html,hello"));
        // Not the blank page: a real document served from a host that happens to be named for it.
        assertFalse(TaffyNavigationProjection.hasBeenNowhere("https://about.test/blank"));
    }

    @Test
    public void aTabThatHasBeenNowhereReportsNoTitleRatherThanItsAddress() {
        // The engine answers `Tab.getTitle()` with the address for a blank document. Carrying that
        // across would put `about:blank` on screen SCR-101 and into a screen reader.
        assertEquals("", titleFor(TaffyNavigationProjection.BLANK_PAGE, "about:blank"));
        assertEquals("", titleFor("", ""));
    }

    @Test
    public void aPageMayCallItselfWhateverItLikes() {
        // The rule is about the address, never about what the string looks like: a real page whose
        // author titled it `about:blank` keeps that title, because it is the page's own word.
        assertEquals("about:blank", titleFor("https://example.test/", "about:blank"));
        assertEquals("Example", titleFor("https://example.test/", "Example"));
        // A page with no title of its own still reports none; nothing is invented here.
        assertEquals("", titleFor("https://example.test/", ""));
    }

    @Test
    public void anHttpsPageIsSecureAndACertificateFailureIsNot() {
        assertTrue(
                TaffyNavigationProjection.of(
                                "https://example.test/",
                                "Example",
                                /* canGoBack= */ false,
                                /* canGoForward= */ false,
                                /* isLoading= */ false,
                                NetError.OK,
                                /* filteringActive= */ false,
                                /* siteExcepted= */ false,
                                /* blockedRequestCount= */ 0)
                        .isSecure());
        assertFalse(
                TaffyNavigationProjection.of(
                                "http://example.test/",
                                "Example",
                                /* canGoBack= */ false,
                                /* canGoForward= */ false,
                                /* isLoading= */ false,
                                NetError.OK,
                                /* filteringActive= */ false,
                                /* siteExcepted= */ false,
                                /* blockedRequestCount= */ 0)
                        .isSecure());
        assertFalse(
                TaffyNavigationProjection.of(
                                "https://example.test/",
                                "Example",
                                /* canGoBack= */ false,
                                /* canGoForward= */ false,
                                /* isLoading= */ false,
                                NetError.ERR_CERT_COMMON_NAME_INVALID,
                                /* filteringActive= */ false,
                                /* siteExcepted= */ false,
                                /* blockedRequestCount= */ 0)
                        .isSecure());
        assertFalse(
                TaffyNavigationProjection.of(
                                "https://example.test/",
                                "Example",
                                /* canGoBack= */ false,
                                /* canGoForward= */ false,
                                /* isLoading= */ false,
                                NetError.ERR_NAME_NOT_RESOLVED,
                                /* filteringActive= */ false,
                                /* siteExcepted= */ false,
                                /* blockedRequestCount= */ 0)
                        .isSecure());
        assertFalse(
                TaffyNavigationProjection.of(
                                "https://example.test/",
                                "Example",
                                /* canGoBack= */ false,
                                /* canGoForward= */ false,
                                /* isLoading= */ false,
                                NetError.ERR_CONNECTION_REFUSED,
                                /* filteringActive= */ false,
                                /* siteExcepted= */ false,
                                /* blockedRequestCount= */ 0)
                        .isSecure());
    }

    @Test
    public void theRestOfTheStateIsCarriedThroughUntouched() {
        NavigationState state =
                TaffyNavigationProjection.of(
                        "https://example.test/",
                        "A page title",
                        /* canGoBack= */ true,
                        /* canGoForward= */ false,
                        /* isLoading= */ true,
                        NetError.OK,
                        /* filteringActive= */ true,
                        /* siteExcepted= */ false,
                        /* blockedRequestCount= */ 7);
        assertEquals("A page title", state.getTitle());
        assertTrue(state.getCanGoBack());
        assertFalse(state.getCanGoForward());
        assertTrue(state.isLoading());
        // The filtering facts are the browser's own numbers, carried through
        // rather than recomputed: this mapping owns words, never counts.
        assertTrue(state.getFilteringActive());
        assertEquals(7, state.getBlockedRequestCount());
    }

    @Test
    public void nothingShownIsBlankInEveryField() {
        // What the surfaces are told when every tab is closed. It has to be distinguishable from a
        // page that loaded with an empty title: nothing is loading and nothing failed either.
        NavigationState state = TaffyNavigationProjection.nothingShown();
        assertEquals("", state.getHost());
        assertEquals("", state.getTitle());
        assertFalse(state.getCanGoBack());
        assertFalse(state.getCanGoForward());
        assertFalse(state.isLoading());
        assertNull(state.getFailure());
        // No tab means no page for blocking to act on, and nothing blocked.
        assertFalse(state.getFilteringActive());
        assertEquals(0, state.getBlockedRequestCount());
        assertEquals("", state.getCanonicalUrl());
    }

    private static String hostFor(String url) {
        return TaffyNavigationProjection.of(
                        url,
                        "",
                        /* canGoBack= */ false,
                        /* canGoForward= */ false,
                        /* isLoading= */ false,
                        NetError.OK,
                        /* filteringActive= */ false,
                        /* siteExcepted= */ false,
                        /* blockedRequestCount= */ 0)
                .getHost();
    }

    private static String titleFor(String url, String title) {
        return TaffyNavigationProjection.of(
                        url,
                        title,
                        /* canGoBack= */ false,
                        /* canGoForward= */ false,
                        /* isLoading= */ false,
                        NetError.OK,
                        /* filteringActive= */ false,
                        /* siteExcepted= */ false,
                        /* blockedRequestCount= */ 0)
                .getTitle();
    }

    private static String canonicalUrlFor(String url) {
        return TaffyNavigationProjection.of(
                        url,
                        "",
                        /* canGoBack= */ false,
                        /* canGoForward= */ false,
                        /* isLoading= */ false,
                        NetError.OK,
                        /* filteringActive= */ false,
                        /* siteExcepted= */ false,
                        /* blockedRequestCount= */ 0)
                .getCanonicalUrl();
    }
}
