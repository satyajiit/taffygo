// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * What an intent from another application asks TaffyGo to open, decided without a browser.
 *
 * <p><b>Why this is its own class.</b> {@link TaffyInboundIntentActivity} is the target of
 * {@code com.google.android.apps.chrome.IntentDispatcher} (patch
 * {@code chromium/patches/0025-taffygo-owns-inbound-intents.md}), which carries nineteen intent
 * filters that any application on the device can reach. The rule that decides what those nineteen
 * may make this browser do is the security boundary of the whole entry path, and a boundary that
 * can only be exercised by installing an APK and running {@code am start} is a boundary that is not
 * exercised. Everything here is strings, so {@code TaffyInboundIntentTest} drives every case on a
 * laptop.
 *
 * <p><b>Nothing here may name {@code GURL}, {@code Uri} or anything with a native half.</b> That is
 * not a style preference: this class is evaluated inside {@code onCreate} of an activity that runs
 * before {@code ChromeBrowserInitializer} has loaded a library, so a URL parser with a native side
 * is not available to it. The scheme test below is therefore deliberate string arithmetic — find
 * the first {@code ':'} that precedes any {@code '/'}, {@code '?'} or {@code '#'} — which is the
 * one part of RFC 3986 that can be decided without parsing the rest.
 *
 * <p><b>The scheme rule is an allowlist, and that is the load-bearing decision.</b> Upstream's
 * {@code IntentHandler.shouldIgnoreIntent} works the other way round: it accepts what it cannot
 * prove hostile, and leans on {@code ExternalIntentUrlChecker.isUnsafeExternalIntentUrl} — which
 * takes a {@code GURL} and therefore needs native — to name the schemes it will not take from a
 * stranger. An allowlist needs no native, is shorter, and fails towards "TaffyGo opened" rather
 * than towards "an application chose what TaffyGo loaded". The entries are exactly the schemes the
 * manifest's own filters advertise and TaffyGo can render:
 *
 * <ul>
 *   <li>{@code http} and {@code https} — the ordinary case, and the only one verified on a device;
 *   <li>{@code content} and {@code file} — the local-document filters (HTML, MHTML,
 *       {@code message/rfc822}, web bundles). Honouring them is not extra scope: the manifest
 *       advertises the handler, so refusing them would be advertising something TaffyGo does not
 *       do. A {@code file:} address from an arbitrary application can name any world-readable path,
 *       which is the same exposure upstream Chrome carries for the same filters, and is named here
 *       rather than discovered later;
 *   <li>{@code about:blank}, exactly, because that is TaffyGo's own name for an empty tab
 *       ({@link TaffyNavigationProjection#BLANK_PAGE}) and the manifest advertises the
 *       {@code about} scheme. No other {@code about:} address is accepted — upstream rewrites most
 *       of them into {@code chrome://} pages, which is the one thing an allowlist exists to keep
 *       out.
 * </ul>
 *
 * <p>Everything else is refused and TaffyGo opens with no new tab: {@code chrome:},
 * {@code javascript:}, {@code data:}, {@code intent:}, and {@code googlechrome:} — the last of
 * which the manifest does advertise on this channel, and which TaffyGo declines on the plainer
 * ground that it is another product's name.
 *
 * <p><b>A refusal is not silence about a failure.</b> It is the same outcome as an application
 * launching TaffyGo with no address at all, which is a thing a person does every day by tapping the
 * icon. What it never is, and what this class exists to make impossible, is Chrome's first run,
 * Chrome's toolbar, or a page of somebody else's choosing loaded because a scheme went unchecked.
 *
 * <p><b>A search is the one thing that leaves here as words rather than as an address.</b> The
 * alias carries {@code WEB_SEARCH}, {@code SEARCH} and {@code MEDIA_SEARCH}, and all three arrive
 * with {@code SearchManager.QUERY} — text, which may name a site or may be a question. Deciding
 * which is {@code AddressBarResolver}'s job and it already does it for everything typed into
 * TaffyGo's address bar, so this class carries the words across untouched and
 * {@link TaffyInboundQuery} hands them to the same resolver. That is what makes the honest refusal
 * TaffyGo already has — it has chosen no search engine, which is OD-019 — apply to the system's
 * search widget as well as to its own address bar, instead of the words being dropped.
 */
public final class TaffyInboundIntent {

    /**
     * The one extra {@link TaffyInboundIntentActivity} puts on the intent it builds, and the only
     * thing {@link TaffyBrowserActivity} reads off it.
     *
     * <p>Namespaced to this class rather than borrowed from {@code IntentHandler}, because the
     * intent it travels on is one TaffyGo constructs and hands to itself. Upstream's extras carry
     * upstream's own trust rules with them, and reusing one would mean inheriting a contract this
     * path does not implement.
     */
    public static final String EXTRA_URL = "org.chromium.taffy.shell.INBOUND_URL";

    /**
     * The other extra: text a person asked TaffyGo to search for.
     *
     * <p>Separate from {@link #EXTRA_URL} because the two mean different things and are acted on by
     * different machinery. An address is opened; a query is handed to the same resolver TaffyGo's
     * own address bar uses, which decides between going somewhere and saying that TaffyGo has no
     * search engine. Never both.
     */
    public static final String EXTRA_QUERY = "org.chromium.taffy.shell.INBOUND_QUERY";

    /** {@code android.content.Intent#ACTION_SEND}, spelled out so this class stays host-pure. */
    private static final String ACTION_SEND = "android.intent.action.SEND";

    /**
     * The three actions on this alias that arrive carrying {@code SearchManager.QUERY} — words
     * rather than an address.
     *
     * <p>{@code WEB_SEARCH} is the one a person reaches: it is what the system search widget and
     * an assistant send, and until patch 0025 it resolved to {@code SearchActivity}, Chrome's own
     * search surface, with Chrome's {@code url_bar} and {@code toolbar} on screen under TaffyGo's
     * name. {@code SEARCH} and {@code MEDIA_SEARCH} are the alias's own two and carry the same
     * extra, so they are read the same way rather than differently for no reason.
     *
     * <p>{@code android.speech.action.VOICE_SEARCH_RESULTS} is deliberately not here. It carries
     * recognizer results in a bundle of parallel arrays rather than a query string, and reading it
     * is upstream's {@code IntentHandler.getUrlFromVoiceSearchResult}. TaffyGo opens for it and
     * does nothing else, which is stated in the patch rather than left to be discovered.
     */
    private static final String[] QUERY_ACTIONS = {
        "android.intent.action.WEB_SEARCH",
        "android.intent.action.SEARCH",
        "android.intent.action.MEDIA_SEARCH",
    };

    /** The one MIME type the share filter in the manifest declares. */
    private static final String TEXT_PLAIN = "text/plain";

    private static final String HTTP_PREFIX = "http://";
    private static final String HTTPS_PREFIX = "https://";

    /**
     * The schemes an application that is not TaffyGo may ask for. See this class's own
     * documentation for why each one is here; {@code about:blank} is handled separately, because it
     * is one address rather than a scheme.
     */
    private static final String[] ALLOWED_SCHEMES = {"http", "https", "content", "file"};

    /** The address to open, or null when the intent asked for nothing TaffyGo will take. */
    private final @Nullable String mUrl;

    /** Words to hand to the address bar's own resolver, or null. Never set together with a URL. */
    private final @Nullable String mQuery;

    private TaffyInboundIntent(@Nullable String url, @Nullable String query) {
        mUrl = url;
        mQuery = query;
    }

    /**
     * Reads an inbound intent's five interesting fields and answers what TaffyGo should do.
     *
     * @param action the intent's action, or null.
     * @param dataString the intent's data as a string — {@code Intent.getDataString()}, already
     *     the caller's problem to have produced, never re-parsed here.
     * @param type the intent's MIME type, or null. Only {@code text/plain} on a share is read.
     * @param sharedText {@code Intent.EXTRA_TEXT}, or null. Only read for a {@code text/plain}
     *     share.
     * @param searchQuery {@code SearchManager.QUERY}, or null. Only read for the actions in
     *     {@link #QUERY_ACTIONS}.
     * @return the decision. Never null; ask {@link #opensUrl()} and {@link #submitsQuery()} what it
     *     says.
     */
    public static TaffyInboundIntent of(
            @Nullable String action,
            @Nullable String dataString,
            @Nullable String type,
            @Nullable String sharedText,
            @Nullable String searchQuery) {
        // A search asks for words, and words are not this class's to interpret: whether they name
        // a site or are a question is AddressBarResolver's decision, and it is the same decision
        // whether they arrived through the address bar or through the system's search widget.
        if (isQueryAction(action) && searchQuery != null) {
            String query = searchQuery.trim();
            if (!query.isEmpty()) return new TaffyInboundIntent(null, query);
        }

        String candidate =
                isPlainTextShare(action, type) ? urlInSharedText(sharedText) : dataString;
        if (candidate == null) return NOTHING;
        String trimmed = candidate.trim();
        if (trimmed.isEmpty()) return NOTHING;
        return isAllowed(trimmed) ? new TaffyInboundIntent(trimmed, null) : NOTHING;
    }

    /** Whether this intent named an address TaffyGo will open. */
    public boolean opensUrl() {
        return mUrl != null;
    }

    /** The address to open, or null. See {@link #opensUrl()}. */
    public @Nullable String getUrl() {
        return mUrl;
    }

    /** Whether this intent asked TaffyGo to search for something. */
    public boolean submitsQuery() {
        return mQuery != null;
    }

    /** The words to search for, or null. See {@link #submitsQuery()}. */
    public @Nullable String getQuery() {
        return mQuery;
    }

    private static boolean isQueryAction(@Nullable String action) {
        if (action == null) return false;
        for (String queryAction : QUERY_ACTIONS) {
            if (queryAction.equals(action)) return true;
        }
        return false;
    }

    /** "Open TaffyGo and nothing else", which is the answer to most of what arrives here. */
    private static final TaffyInboundIntent NOTHING = new TaffyInboundIntent(null, null);

    private static boolean isPlainTextShare(@Nullable String action, @Nullable String type) {
        return ACTION_SEND.equals(action) && TEXT_PLAIN.equals(type);
    }

    /**
     * The address inside shared text, by upstream's own rule.
     *
     * <p>{@code IntentHandler.getUrlFromShareIntent} collects every {@code http://}-prefixed token
     * and then every {@code https://}-prefixed one into a single list and takes the last, which
     * both prefers the end of the message — where a share usually puts the link — and prefers
     * https over http when a message carries both. Reproducing the ordering rather than inventing a
     * tidier one keeps a shared link landing on the same address it lands on in Chrome.
     *
     * <p>Upstream has a second half this does not: when no literal URL is present it asks the
     * autocomplete system to classify the text and falls back to a search query. Both need a
     * profile and a native library, so neither is reachable from here — and a search TaffyGo cannot
     * run is not a search it should pretend to. Shared text with no link in it opens TaffyGo.
     */
    private static @Nullable String urlInSharedText(@Nullable String text) {
        if (text == null || text.isEmpty()) return null;
        List<String> urls = new ArrayList<>();
        collectTokensWithPrefix(text, HTTP_PREFIX, urls);
        collectTokensWithPrefix(text, HTTPS_PREFIX, urls);
        return urls.isEmpty() ? null : urls.get(urls.size() - 1);
    }

    /** Every whitespace-terminated token starting with {@code prefix}, appended to {@code out}. */
    private static void collectTokensWithPrefix(String text, String prefix, List<String> out) {
        int i = 0;
        while (i < text.length()) {
            int start = text.indexOf(prefix, i);
            if (start == -1) return;
            int end = start + prefix.length();
            while (end < text.length() && !Character.isWhitespace(text.charAt(end))) end++;
            out.add(text.substring(start, end));
            i = end;
        }
    }

    /**
     * Whether an address is one TaffyGo will take from an application that is not TaffyGo.
     *
     * <p>{@code about:blank} is compared whole rather than by scheme, which is the difference
     * between "TaffyGo's empty tab" and "every {@code about:} address upstream maps to a
     * {@code chrome://} page".
     */
    private static boolean isAllowed(String url) {
        if (TaffyNavigationProjection.BLANK_PAGE.equalsIgnoreCase(url)) return true;
        String scheme = schemeOf(url);
        if (scheme == null) return false;
        for (String allowed : ALLOWED_SCHEMES) {
            if (allowed.equals(scheme)) return true;
        }
        return false;
    }

    /**
     * The scheme of an address, lowercased, or null if it has none.
     *
     * <p>A scheme is everything before the first {@code ':'}, and only if that colon comes before
     * any {@code '/'}, {@code '?'} or {@code '#'} — otherwise the colon belongs to a path, a query
     * or a fragment and the string is a relative reference with no scheme at all. That is the whole
     * of RFC 3986 that has to be true here, and it is decided without a parser so that this file
     * can run before native does.
     */
    private static @Nullable String schemeOf(String url) {
        for (int i = 0; i < url.length(); i++) {
            char c = url.charAt(i);
            if (c == ':') return i == 0 ? null : url.substring(0, i).toLowerCase(Locale.ROOT);
            if (c == '/' || c == '?' || c == '#') return null;
        }
        return null;
    }
}
