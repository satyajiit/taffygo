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

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

/**
 * What another application may make TaffyGo open, checked on a laptop.
 *
 * <p>This is the security boundary of the whole inbound-intent path. Patch
 * {@code chromium/patches/0025-taffygo-owns-inbound-intents.md} points
 * {@code com.google.android.apps.chrome.IntentDispatcher} — nineteen intent filters, reachable by
 * every application on the device — at {@link TaffyInboundIntentActivity}, and everything that
 * activity decides it decides by calling {@link TaffyInboundIntent}. Driving these cases on a
 * device would mean installing an APK and running {@code am start} once per row below, which is why
 * the rule was written with no Android and no native in it: so that this suite can exist and can
 * fail before a build does.
 *
 * <p>Three properties are asserted throughout rather than stated once. Every refusal answers "open
 * TaffyGo with no address" rather than throwing, because an exception on this path would be a crash
 * an arbitrary application could trigger. Every acceptance returns the address unchanged, so
 * nothing here can quietly rewrite what a person asked for. And an address and a search query are
 * never both set, which is what lets the activity branch on one and return.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyInboundIntentTest {

    private static final String VIEW = "android.intent.action.VIEW";
    private static final String SEND = "android.intent.action.SEND";
    private static final String SEARCH = "android.intent.action.SEARCH";
    private static final String WEB_SEARCH = "android.intent.action.WEB_SEARCH";
    private static final String MEDIA_SEARCH = "android.intent.action.MEDIA_SEARCH";
    private static final String TEXT_PLAIN = "text/plain";

    /** A {@code VIEW} intent with a data string and nothing else, which is the common case. */
    private static TaffyInboundIntent view(String dataString) {
        return TaffyInboundIntent.of(VIEW, dataString, null, null, null);
    }

    /** A {@code text/plain} share carrying {@code EXTRA_TEXT}. */
    private static TaffyInboundIntent share(String sharedText) {
        return TaffyInboundIntent.of(SEND, null, TEXT_PLAIN, sharedText, null);
    }

    /** A search, of whichever kind, carrying {@code SearchManager.QUERY}. */
    private static TaffyInboundIntent search(String action, String query) {
        return TaffyInboundIntent.of(action, null, null, null, query);
    }

    private static void assertOpens(String expectedUrl, TaffyInboundIntent decision) {
        assertTrue("expected to open " + expectedUrl, decision.opensUrl());
        assertEquals(expectedUrl, decision.getUrl());
        assertFalse("an address must not also be a query", decision.submitsQuery());
        assertNull(decision.getQuery());
    }

    private static void assertRefuses(TaffyInboundIntent decision) {
        assertFalse("expected a refusal, got " + decision.getUrl(), decision.opensUrl());
        assertNull(decision.getUrl());
        assertFalse("a refusal must not also be a query", decision.submitsQuery());
        assertNull(decision.getQuery());
    }

    private static void assertSubmits(String expectedQuery, TaffyInboundIntent decision) {
        assertTrue("expected to submit " + expectedQuery, decision.submitsQuery());
        assertEquals(expectedQuery, decision.getQuery());
        // A query and an address are alternatives. The activity's own branch returns rather than
        // falling through, and this is the other half of that guarantee.
        assertFalse("a query must not also be an address", decision.opensUrl());
        assertNull(decision.getUrl());
    }

    // -----------------------------------------------------------------------
    // The ordinary case: a link tapped in another application.
    // -----------------------------------------------------------------------

    @Test
    public void anHttpsLinkOpens() {
        assertOpens("https://example.net/", view("https://example.net/"));
    }

    @Test
    public void anHttpLinkOpens() {
        assertOpens("http://example.net/a?b=c#d", view("http://example.net/a?b=c#d"));
    }

    @Test
    public void aSchemeIsRecognisedWhateverItsCase() {
        // Android does not lowercase a scheme before handing it over, and a case-sensitive test
        // here would refuse a perfectly ordinary link from an application that shouted.
        assertOpens("HTTPS://example.net/", view("HTTPS://example.net/"));
    }

    @Test
    public void surroundingWhitespaceIsTrimmedRatherThanRefused() {
        assertOpens("https://example.net/", view("  https://example.net/\n"));
    }

    // -----------------------------------------------------------------------
    // The local-document filters the manifest advertises.
    // -----------------------------------------------------------------------

    @Test
    public void aContentDocumentOpens() {
        assertOpens("content://media/external/file/42", view("content://media/external/file/42"));
    }

    @Test
    public void aLocalFileOpens() {
        assertOpens("file:///sdcard/Download/page.html", view("file:///sdcard/Download/page.html"));
    }

    @Test
    public void aboutBlankOpens() {
        // TaffyGo's own name for an empty tab, and the one `about:` address that is not a
        // redirection into an internal page.
        assertOpens(TaffyNavigationProjection.BLANK_PAGE, view("about:blank"));
    }

    // -----------------------------------------------------------------------
    // THE REFUSALS. Each row is a way an application could have chosen what this browser loads.
    // -----------------------------------------------------------------------

    @Test
    public void anInternalChromePageIsRefused() {
        assertRefuses(view("chrome://settings"));
        assertRefuses(view("chrome-untrusted://feed"));
    }

    @Test
    public void aTaffyInternalPageIsRefusedTheSameWay() {
        // Nothing serves this scheme today. It is asserted so that the day something does, the
        // allowlist is what has to be changed on purpose rather than a door that was already open.
        assertRefuses(view("taffygo://settings"));
    }

    @Test
    public void scriptAndInlineDataAreRefused() {
        assertRefuses(view("javascript:alert(1)"));
        assertRefuses(view("data:text/html,<script>alert(1)</script>"));
    }

    @Test
    public void anIntentUrlIsRefused() {
        // No filter at the pin advertises intent://, but IntentHandler parses the scheme when one
        // reaches it, and a nested intent is the redirection primitive this path must not have.
        assertRefuses(view("intent://example.net#Intent;scheme=https;end"));
    }

    @Test
    public void anotherProductsSelfSchemeIsRefused() {
        // The manifest does advertise googlechrome: on this channel. TaffyGo declines it: upstream
        // unwraps it into a real URL, and TaffyGo is not going to answer to another product's name
        // to do so.
        assertRefuses(view("googlechrome://navigate?url=https://example.net/"));
    }

    @Test
    public void everyAboutAddressExceptBlankIsRefused() {
        assertRefuses(view("about:version"));
        assertRefuses(view("about:blank#not-blank"));
    }

    @Test
    public void anIntentWithNoDataAtAllOpensTheBrowser() {
        assertRefuses(TaffyInboundIntent.of("android.intent.action.MAIN", null, null, null, null));
        assertRefuses(view(null));
        assertRefuses(view(""));
        assertRefuses(view("   "));
    }

    @Test
    public void aRelativeReferenceHasNoSchemeAndIsRefused() {
        assertRefuses(view("example.net/path"));
        assertRefuses(view("/etc/passwd"));
        // The colon here belongs to a path segment, not to a scheme, so this string names no
        // scheme at all and must not be read as one.
        assertRefuses(view("/a:b/c"));
        assertRefuses(view(":no-scheme"));
    }

    // -----------------------------------------------------------------------
    // Sharing text, which is the second-most-common way a link arrives.
    // -----------------------------------------------------------------------

    @Test
    public void aSharedLinkOpens() {
        assertOpens("https://example.net/story", share("Look at this https://example.net/story"));
    }

    @Test
    public void aSharedLinkIsTakenFromTheEndOfTheMessage() {
        assertOpens(
                "https://example.net/second",
                share("https://example.net/first and then https://example.net/second"));
    }

    @Test
    public void httpsWinsOverHttpWhenAMessageCarriesBoth() {
        // Upstream's own ordering in IntentHandler.getUrlFromShareIntent: http tokens are collected
        // first, https tokens second, and the last of the combined list wins. Reproducing the rule
        // keeps a shared link landing where Chrome would have put it.
        assertOpens(
                "https://example.net/secure",
                share("https://example.net/secure then http://example.net/plain"));
    }

    @Test
    public void aSharedLinkStopsAtWhitespace() {
        assertOpens("https://example.net/a", share("see https://example.net/a now"));
    }

    @Test
    public void sharedTextWithNoLinkOpensTheBrowser() {
        // Upstream would classify this through autocomplete and fall back to a search query. Both
        // need a profile and a native library, and neither is reachable before initialization —
        // so TaffyGo opens rather than pretending to have searched.
        assertRefuses(share("just some words"));
        assertRefuses(share(""));
        assertRefuses(share(null));
    }

    @Test
    public void aShareWithoutTextPlainReadsItsDataInstead() {
        // The manifest's share filter is text/plain only; a SEND with another type that still
        // carries a data URI is read the ordinary way rather than having its EXTRA_TEXT scanned.
        assertOpens(
                "https://example.net/",
                TaffyInboundIntent.of(SEND, "https://example.net/", "image/png", "ignored", null));
    }

    @Test
    public void sharedTextIsIgnoredForEveryActionThatIsNotSend() {
        // A VIEW intent carrying EXTRA_TEXT must not have the text mined for a URL: the data string
        // is what a VIEW means, and reading anything else would let a caller smuggle an address
        // past the filter that admitted the intent.
        assertRefuses(TaffyInboundIntent.of(VIEW, null, TEXT_PLAIN, "https://example.net/", null));
        assertRefuses(TaffyInboundIntent.of(SEARCH, null, TEXT_PLAIN, "https://example.net/", null));
    }

    // -----------------------------------------------------------------------
    // The other filters on the alias, none of which carries an address TaffyGo can use.
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // SEARCHES. The one door that arrives as words, and the one patch 0025 took off
    // SearchActivity — Chrome's own search surface, with Chrome's url_bar and toolbar on it.
    // -----------------------------------------------------------------------

    @Test
    public void aWebSearchCarriesItsWordsAcross() {
        assertSubmits("98 inch tv price", search(WEB_SEARCH, "98 inch tv price"));
    }

    @Test
    public void theOtherTwoSearchActionsAreReadTheSameWay() {
        // Both are on the same alias and both carry SearchManager.QUERY, so reading them
        // differently would be a difference with no reason behind it.
        assertSubmits("toffee recipe", search(SEARCH, "toffee recipe"));
        assertSubmits("toffee recipe", search(MEDIA_SEARCH, "toffee recipe"));
    }

    @Test
    public void aSearchQueryIsTrimmedAndAnEmptyOneIsNotASearch() {
        assertSubmits("toffee", search(WEB_SEARCH, "  toffee \n"));
        assertRefuses(search(WEB_SEARCH, "   "));
        assertRefuses(search(WEB_SEARCH, ""));
        assertRefuses(search(WEB_SEARCH, null));
    }

    @Test
    public void aQueryThatLooksLikeAnAddressIsStillCarriedAsWords() {
        // Deliberate: whether words name a site or ask a question is AddressBarResolver's
        // decision, and it is already the decision TaffyGo makes for everything typed into the
        // address bar. Answering it twice, in two places, is how the two answers start to differ.
        assertSubmits("https://example.net/", search(WEB_SEARCH, "https://example.net/"));
        assertSubmits("example.net", search(WEB_SEARCH, "example.net"));
    }

    @Test
    public void aQueryIsIgnoredForEveryActionThatIsNotASearch() {
        // Otherwise any caller could smuggle words past the filter that admitted the intent by
        // attaching SearchManager.QUERY to something else.
        assertRefuses(TaffyInboundIntent.of(VIEW, null, null, null, "toffee"));
        assertRefuses(TaffyInboundIntent.of(SEND, null, TEXT_PLAIN, null, "toffee"));
    }

    @Test
    public void voiceSearchResultsCarryNoQueryAndOpenTheBrowser() {
        // VOICE_SEARCH_RESULTS is on the alias but carries recognizer results in parallel arrays
        // rather than a query string. TaffyGo opens and does nothing else, which is stated in the
        // patch rather than left to be discovered.
        assertRefuses(
                TaffyInboundIntent.of(
                        "android.speech.action.VOICE_SEARCH_RESULTS", null, null, null, "toffee"));
    }

    @Test
    public void anNfcTagCarryingAWebAddressOpensIt() {
        // NDEF_DISCOVERED is filtered on http and https data, so it needs no special case — it is
        // asserted because the filter exists and a reader will look for it.
        assertOpens(
                "https://example.net/tag",
                TaffyInboundIntent.of(
                        "android.nfc.action.NDEF_DISCOVERED",
                        "https://example.net/tag",
                        null,
                        null,
                        null));
    }

    @Test
    public void theExtraNameIsNamespacedToTaffyGo() {
        // The extra travels on an intent TaffyGo builds and hands to itself. Borrowing one of
        // upstream's names would mean inheriting the trust rules that go with it, so this asserts
        // the name stays TaffyGo's own.
        assertTrue(
                TaffyInboundIntent.EXTRA_URL.startsWith("org.chromium.taffy.shell."));
        assertTrue(
                TaffyInboundIntent.EXTRA_QUERY.startsWith("org.chromium.taffy.shell."));
    }
}
