// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

import com.taffygo.browser.ui.core.browser.NavigationState;
import com.taffygo.browser.ui.core.model.PageLoadFailure;

import org.chromium.net.NetError;

/**
 * What the engine is doing, in the words the screens speak. Pure, and therefore testable anywhere.
 *
 * <p><b>Why this is its own class rather than four lines inside the mediator.</b> Everything else
 * in {@code ChromiumBrowserMediator} needs a live browser: a {@code TabModelSelector}, a
 * {@code Tab}, a {@code GURL} whose accessors are JNI calls. This does not. It takes strings,
 * booleans and one integer, and returns the UI layer's own {@link NavigationState} — so the mapping
 * that decides <i>which words a person is shown when a page fails</i> is checked on a laptop, by
 * {@code TaffyNavigationProjectionTest}, rather than only on a device that has to be able to fail
 * a DNS lookup on demand.
 *
 * <p>Nothing here may name {@code GURL}, a {@code Tab}, or anything with a native half. The host
 * parsing below is deliberately string arithmetic over a spec that has <i>already</i> been through
 * {@code GURL} on the caller's side, not a second URL parser: it splits a canonical spec, and a
 * caller that hands it something else gets a host it can see is wrong rather than a silent
 * mis-parse.
 *
 * <p><b>THE FAILURE MAPPING IS DELIBERATELY PARTIAL, AND THIS IS WHERE THAT IS STATED.</b>
 * {@link PageLoadFailure} has six values, because screen SCR-108 has six sets of words —
 * offline, name not resolved, unreachable, timed out, certificate invalid, and a page that
 * stopped after it had loaded. The network stack has several hundred error codes. This maps the
 * ones those first five words are true of, plus the one sentinel {@link #PAGE_CRASHED} that is
 * not a network error at all, and returns {@code null} for every other, which means "TaffyGo has
 * no wording of its own for this one".
 *
 * <p>Returning {@code null} there is not a claim that the page loaded. Chromium renders its own
 * error page into the {@code WebContents} for a failed navigation, and TaffyGo does not intercept
 * it — so what a person sees for an unmapped failure is the engine's error page, in the page
 * surface, which is a true description of what happened. For the six this <i>can</i> name,
 * TaffyGo's own notice is drawn over it, which is the split the model records: upstream owns the
 * behaviour, TaffyGo owns the wording.
 */
public final class TaffyNavigationProjection {

    /**
     * "This navigation did not fail", in the engine's own vocabulary.
     *
     * <p>Published here so that {@code ChromiumBrowserMediator} — which is Kotlin — can say it
     * without naming {@link NetError}, a Java annotation type whose constants Kotlin reaches
     * awkwardly. It is {@link NetError#OK} and the compiler checks that it is.
     */
    public static final int NO_ERROR = NetError.OK;

    /**
     * Not a network error. The mediator writes this when the renderer dies, so
     * {@link #failureOf} can name {@link PageLoadFailure#PAGE_CRASHED} the same
     * way it names the four load failures. {@link Integer#MIN_VALUE} is outside
     * every code {@code net/base/net_error_list.h} has ever used.
     */
    public static final int PAGE_CRASHED = Integer.MIN_VALUE;

    /**
     * The address a tab carries when it has not been anywhere.
     *
     * <p>Published because three places in this package have to agree on it and a second literal
     * is a second answer: {@code TaffyBrowserActivity} opens every new tab on it,
     * {@link #hasBeenNowhere} recognises it, and {@code ChromiumBrowserMediator} asks that question
     * of every tab it projects.
     */
    public static final String BLANK_PAGE = "about:blank";

    /** Nothing to construct: this is a mapping, and a mapping has no state. */
    private TaffyNavigationProjection() {}

    /**
     * Whether this address means the tab has not been anywhere yet.
     *
     * <p><b>This is what stops {@code about:blank} being spoken to a person.</b> The engine has no
     * concept of "no page": a tab with nothing in it is a tab on {@link #BLANK_PAGE}, and
     * {@code Tab.getTitle()} answers with that same string because a blank document has no title
     * of its own to give. Passing it through would put an internal address into a card, a title and
     * — worst of the three — a screen reader's announcement, which
     * {@code docs/voice-and-naming.md} forbids: it is not a name, it is a mechanism.
     *
     * <p>The empty spec counts too. That is what a tab reports before its first navigation has
     * committed, and it means exactly the same thing.
     *
     * @param url the canonical spec of the page, as {@code GURL.getSpec()} gives it.
     */
    public static boolean hasBeenNowhere(String url) {
        return url.isEmpty() || BLANK_PAGE.equals(url);
    }

    /** What the browsing surfaces are told when no tab is selected. */
    public static NavigationState nothingShown() {
        return new NavigationState(
                /* host= */ "",
                /* title= */ "",
                /* canGoBack= */ false,
                /* canGoForward= */ false,
                /* isLoading= */ false,
                /* failure= */ null,
                /* isSecure= */ false,
                /* filteringActive= */ false,
                /* siteExcepted= */ false,
                /* blockedRequestCount= */ 0,
                /* canonicalUrl= */ "");
    }

    /**
     * The selected tab, as the browsing surfaces see it.
     *
     * @param url the canonical spec of the page, as {@code GURL.getSpec()} gives it. May be empty.
     * @param title the title the page gave, or the engine's fallback. May be empty.
     * @param canGoBack whether there is history behind this page.
     * @param canGoForward whether there is history ahead of it.
     * @param isLoading whether the page is still arriving.
     * @param netErrorCode the last error this navigation reported, or {@link NetError#OK}.
     * @param filteringActive whether ad and tracker blocking is acting on this page.
     * @param siteExcepted whether a person has allowed this page's site on the plane this tab
     *     belongs to, whatever the master toggle says.
     * @param blockedRequestCount requests blocked on this page so far, coalesced by the browser.
     */
    public static NavigationState of(
            String url,
            String title,
            boolean canGoBack,
            boolean canGoForward,
            boolean isLoading,
            int netErrorCode,
            boolean filteringActive,
            boolean siteExcepted,
            int blockedRequestCount) {
        PageLoadFailure failure = failureOf(netErrorCode);
        return new NavigationState(
                hostOf(url),
                titleOf(url, title),
                canGoBack,
                canGoForward,
                isLoading,
                failure,
                isSecure(url, failure),
                filteringActive,
                siteExcepted,
                blockedRequestCount,
                canonicalHttpUrlOf(url));
    }

    /**
     * The exact committed address that page actions may hand outside the browser.
     *
     * <p>The caller has already canonicalised this string through {@code GURL}; this method only
     * applies the narrower product boundary. Page actions accept HTTP(S), require a real host, and
     * refuse userinfo rather than carrying credentials or an address-bar spoof into a share or
     * bookmark. The accepted string is returned untouched, including its path, query and fragment.
     */
    private static String canonicalHttpUrlOf(String url) {
        if (!url.startsWith("http://") && !url.startsWith("https://")) return "";
        int authorityStart = url.indexOf("://") + 3;
        int authorityEnd = url.length();
        for (int i = authorityStart; i < url.length(); i++) {
            char c = url.charAt(i);
            if (c == '/' || c == '?' || c == '#') {
                authorityEnd = i;
                break;
            }
        }
        String authority = url.substring(authorityStart, authorityEnd);
        if (authority.isEmpty() || authority.indexOf('@') >= 0 || hostOf(url).isEmpty()) return "";
        return url;
    }

    /**
     * The title, which is the page's own words or nothing at all.
     *
     * <p>A tab that {@link #hasBeenNowhere} has no title, and says so with the empty string rather
     * than with the engine's fallback. The surfaces already draw their own words for a tab with no
     * title — screen SCR-101 shows the address bar's invitation, and screen SCR-104's card names
     * itself — so the empty string is a state they render, not a gap they leak.
     *
     * <p>Every other title is the page's, untouched. A page is entitled to call itself whatever it
     * likes, including something that looks like an address.
     */
    private static String titleOf(String url, String title) {
        return hasBeenNowhere(url) ? "" : title;
    }

    /**
     * The host, which is what the address pill shows instead of a full URL.
     *
     * <p>Userinfo is dropped rather than displayed: {@code https://apple.com@evil.test/} is a page
     * on {@code evil.test}, and showing the part before the {@code @} is the oldest address-bar
     * spoof there is. The port is dropped too, because it is not part of the identity a person is
     * being asked to recognise.
     *
     * <p>An address with no host — {@code about:blank}, a {@code data:} URL, a {@code file:} path —
     * has no host to show and gets the empty string. The surfaces already draw their placeholder
     * for that, which is the right answer for the blank page a new tab starts on.
     */
    private static String hostOf(String url) {
        int schemeEnd = url.indexOf("://");
        if (schemeEnd < 0) return "";
        String rest = url.substring(schemeEnd + 3);
        int authorityEnd = rest.length();
        for (int i = 0; i < rest.length(); i++) {
            char c = rest.charAt(i);
            if (c == '/' || c == '?' || c == '#') {
                authorityEnd = i;
                break;
            }
        }
        String authority = rest.substring(0, authorityEnd);
        int userInfoEnd = authority.lastIndexOf('@');
        if (userInfoEnd >= 0) authority = authority.substring(userInfoEnd + 1);
        // An IPv6 literal keeps its brackets and its colons; only a port is cut.
        if (authority.startsWith("[")) {
            int literalEnd = authority.indexOf(']');
            return literalEnd < 0 ? authority : authority.substring(0, literalEnd + 1);
        }
        int portStart = authority.indexOf(':');
        return portStart < 0 ? authority : authority.substring(0, portStart);
    }

    /**
     * A private connection, in the words the lock speaks.
     *
     * <p>{@code https} is necessary and not sufficient. A failure means the
     * page never arrived as a private document — a name that did not resolve
     * is not a lock, and a certificate that did not check out is the one case
     * the address began securely and the page is not.
     */
    private static boolean isSecure(String url, @Nullable PageLoadFailure failure) {
        if (failure != null) return false;
        return url.startsWith("https://");
    }

    /**
     * Whether this address is the engine's own error document, not a page the
     * person asked for.
     *
     * <p>Chromium commits {@code chrome-error://chromewebdata/} after a failed
     * navigation. That is a new page load to the observer, and treating it as
     * a retry would forget the failure that produced it. The network-error
     * scheme is the older spelling of the same document.
     */
    public static boolean isErrorDocument(String url) {
        return url.startsWith("chrome-error:") || url.startsWith("chrome://network-error");
    }

    /**
     * Whether an address names one of the engine's own pages rather than a page on the web.
     *
     * <p><b>Typed input cannot reach one, and this is the third thing that says so.</b>
     * {@code AddressBarResolver} navigates only on an allowlist — {@code http}, {@code https},
     * {@code ftp}, {@code ws} and {@code wss} — so {@code chrome://flags} typed into the box is
     * read as a search and never as a location, and {@link TaffyInboundIntent} refuses one
     * arriving from another application for the same reason. Both are allowlists and both are
     * one edit away from admitting a scheme by accident. This is the refusal at both points
     * inside the product where a string becomes a load — {@code ChromiumBrowserMediator}'s
     * {@code navigateTo} and the address a new tab opens on — so a new caller, a suggestion row,
     * a restored tab or a hand-off, reaches an engine page only by deleting one of them by name.
     *
     * <p>These pages are Chromium's own and none of them is a TaffyGo surface: the product draws
     * its own settings, history, downloads and about screens, and a person who reaches
     * {@code chrome://settings} is looking at another product's controls over their profile.
     * They are also not whole here — a Play install carries no {@code dev_ui_resources.pak}, so
     * {@code chrome://net-internals} and its neighbours render empty.
     *
     * <p>{@code about:} is included because the engine rewrites most of it into {@code chrome://},
     * and {@link #BLANK_PAGE} is excluded by name because that is TaffyGo's own word for an empty
     * tab. {@code chrome-error:} is not here either: {@link #isErrorDocument} owns it, the engine
     * commits it after a load that failed, and nothing ever asks for it.
     */
    public static boolean isEngineInternalPage(String url) {
        if (BLANK_PAGE.equalsIgnoreCase(url)) return false;
        for (String scheme : ENGINE_INTERNAL_SCHEMES) {
            if (url.regionMatches(/* ignoreCase= */ true, 0, scheme, 0, scheme.length())) {
                return true;
            }
        }
        return false;
    }

    /**
     * The schemes {@link #isEngineInternalPage} refuses, each with a colon so that a host can
     * never be read as one — {@code chrome.example.com} is a site and {@code chrome:} is not.
     */
    private static final String[] ENGINE_INTERNAL_SCHEMES = {
        "chrome:",
        "chrome-untrusted:",
        "chrome-native:",
        "chrome-search:",
        "chrome-distiller:",
        "devtools:",
        "about:",
    };

    /**
     * Whether a newly started load should forget the last remembered error.
     *
     * <p>A URL the person asked for does. The engine's error document does
     * not: it is the failed load continuing.
     */
    public static boolean clearsRememberedError(String startedUrl) {
        return !isErrorDocument(startedUrl);
    }

    /**
     * The error to keep after a new code arrives for the same tab.
     *
     * <p>{@link NetError#ERR_ABORTED} is the original navigation being
     * replaced — often by the error document — and must not take a named
     * failure with it. A code TaffyGo has no words for must not replace one
     * it does. A crash stays a crash: a follow-on load failure as the
     * sad-tab document commits is not a better description.
     */
    public static int rememberError(int existing, int incoming) {
        if (existing == PAGE_CRASHED) return existing;
        if (incoming == NetError.OK || incoming == NetError.ERR_ABORTED) {
            return existing;
        }
        // ERR_FAILED is the generic leftover. A named reason already held is
        // a better description than "something failed".
        if (incoming == NetError.ERR_FAILED && failureOf(existing) != null) {
            return existing;
        }
        if (failureOf(existing) != null && failureOf(incoming) == null) {
            return existing;
        }
        return incoming;
    }

    /**
     * The six failures TaffyGo has words for, and null for everything else.
     *
     * <p>{@link NetError} is a source-retention {@code @IntDef} of compile-time constants generated
     * from {@code net/base/net_error_list.h}, so every name below is checked by the compiler
     * against the engine's own list and nothing here carries a magic number — except
     * {@link #PAGE_CRASHED}, which is this file's own sentinel for a dead renderer.
     *
     * <p>Published so the mediator can ask whether a remembered code still
     * has wording, without duplicating the table.
     */
    public static @Nullable PageLoadFailure failureOf(int netErrorCode) {
        if (netErrorCode == NetError.OK) return null;

        // Not a failure at all. The renderer reports this when a navigation is replaced by another
        // one or stopped by the user, which is what happens every time somebody types a new
        // address before the last one finished. Showing an error page for it would put a failure
        // notice on top of the page that succeeded.
        if (netErrorCode == NetError.ERR_ABORTED) return null;

        if (netErrorCode == PAGE_CRASHED) return PageLoadFailure.PAGE_CRASHED;

        if (isCertificateError(netErrorCode)) return PageLoadFailure.CERTIFICATE_INVALID;

        switch (netErrorCode) {
            case NetError.ERR_INTERNET_DISCONNECTED:
            case NetError.ERR_NETWORK_CHANGED:
            case NetError.ERR_NETWORK_ACCESS_DENIED:
                return PageLoadFailure.OFFLINE;
            case NetError.ERR_NAME_NOT_RESOLVED:
            case NetError.ERR_NAME_RESOLUTION_FAILED:
            case NetError.ERR_DNS_TIMED_OUT:
            case NetError.ERR_DNS_MALFORMED_RESPONSE:
            case NetError.ERR_DNS_SERVER_FAILURE:
                return PageLoadFailure.NAME_NOT_RESOLVED;
            case NetError.ERR_CONNECTION_REFUSED:
            case NetError.ERR_CONNECTION_RESET:
            case NetError.ERR_CONNECTION_CLOSED:
            case NetError.ERR_CONNECTION_ABORTED:
            case NetError.ERR_CONNECTION_FAILED:
            case NetError.ERR_ADDRESS_UNREACHABLE:
            case NetError.ERR_EMPTY_RESPONSE:
            case NetError.ERR_INVALID_RESPONSE:
            case NetError.ERR_FAILED:
                return PageLoadFailure.UNREACHABLE;
            case NetError.ERR_TIMED_OUT:
            case NetError.ERR_CONNECTION_TIMED_OUT:
                return PageLoadFailure.TIMED_OUT;
            default:
                return null;
        }
    }

    /**
     * The certificate range, spelled the way {@code net::IsCertificateError} spells it.
     *
     * <p>A range rather than a list of codes, because that is what the C++ predicate is
     * ({@code net/base/net_errors.cc}): the certificate errors are a contiguous block in
     * <i>decreasing</i> order from {@code ERR_CERT_COMMON_NAME_INVALID} down to, but not including,
     * {@code ERR_CERT_END}, and a new certificate failure is added inside it. Listing them by name
     * would silently stop covering the next one upstream adds; a range does not.
     *
     * <p>The pinned key exception is upstream's, not this file's: a pin failure is a certificate
     * failure that happens to sit outside the block.
     */
    private static boolean isCertificateError(int netErrorCode) {
        return (netErrorCode <= NetError.ERR_CERT_COMMON_NAME_INVALID
                        && netErrorCode > NetError.ERR_CERT_END)
                || netErrorCode == NetError.ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN;
    }
}
