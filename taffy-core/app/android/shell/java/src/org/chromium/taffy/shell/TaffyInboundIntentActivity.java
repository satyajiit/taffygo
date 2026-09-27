// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;
import android.app.SearchManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;

import androidx.annotation.Nullable;

import org.chromium.base.IntentUtils;
import org.chromium.base.Log;

/**
 * The door every other application knocks on, and the first TaffyGo code an inbound intent meets.
 *
 * <p><b>What this replaces.</b> {@code com.google.android.apps.chrome.IntentDispatcher} is the
 * {@code activity-alias} carrying nineteen intent filters — {@code VIEW} for http, https, about,
 * content and file; {@code SEND} for {@code text/plain}; {@code WEB_SEARCH},
 * {@code MEDIA_SEARCH}, voice search results, {@code NDEF_DISCOVERED}, {@code SEARCH}. Until patch
 * {@code chromium/patches/0025-taffygo-owns-inbound-intents.md} it targeted
 * {@code ChromeLauncherActivity}, whose {@code dispatch()} calls
 * {@code FirstRunFlowSequencer.launch} and then
 * {@code LaunchIntentDispatcher.dispatchToTabbedActivity}. So tapping a link in another application
 * put Chrome's first run — "Welcome to Chrome", a Google Terms of Service link, a Google telemetry
 * disclosure — and then Chrome's own toolbar in front of a person using TaffyGo. Patch 0022 fixed
 * the launcher tap and said in as many words that it did not fix this. This is that door.
 *
 * <p><b>Why an activity at all, when {@link TaffyBrowserActivity} could be the alias's target.</b>
 * Android requires an {@code activity-alias} to name an activity declared <i>before</i> it in the
 * manifest. This alias is near the top of {@code <application>} and TaffyGo's browser activity is
 * four hundred lines below it, so pointing the alias straight at it would mean moving patch 0022's
 * block — a bigger diff, a rewrite of an applied patch, and the loss of the
 * {@code chrome_activity_common} jinja call that keeps TaffyGo's window attributes in step with
 * Chrome's own. The patch specification argues that trade in full.
 *
 * <p><b>What it does, in three steps and no more.</b> Read the intent; decide with
 * {@link TaffyInboundIntent}, which has no Android and no native in it; start
 * {@link TaffyBrowserActivity} with a <b>freshly constructed</b> intent and finish. There is no
 * fourth step and there is deliberately no branch that hands the caller's own intent object
 * onward: forwarding a {@code Parcelable} that arrived from an arbitrary application is the
 * intent-redirection defect class, and the only reliable defence is to never do it. Every field
 * that crosses from the caller's intent to TaffyGo's is a {@code String} this class read, checked
 * and re-wrote.
 *
 * <p><b>What it does not do, measured against what {@code ChromeLauncherActivity} did.</b> Send Tab
 * to Self share forwarding, partner browser customizations, bring-tab-to-front from a Chrome
 * notification, web-search dispatch to {@code SearchActivity}, the WebAPK fallback, Chrome's
 * notification-preferences category and Custom Tab dispatch are all gone from this path. The first
 * six are Chrome features TaffyGo does not have, and reproducing them would be building surfaces
 * nothing asks for. The seventh is a behaviour change worth naming: an application launching a
 * {@code CustomTabsIntent} now gets an ordinary TaffyGo tab instead of Chrome's Custom Tab toolbar.
 * That is the correct answer for this milestone — no third-party application may put another
 * product's interface on screen under TaffyGo's name — and it is emphatically not an implementation
 * of the Custom Tabs protocol.
 *
 * <p><b>The search filter is the one entry point that arrives as words rather than an address.</b>
 * Patch 0025 also moves {@code android.intent.action.WEB_SEARCH} off {@code SearchActivity} — which
 * is Chrome's own search surface and put Chrome's {@code url_bar} and {@code toolbar} on screen —
 * onto this alias. The words are carried across in {@link TaffyInboundIntent#EXTRA_QUERY} and
 * answered by {@link TaffyInboundQuery} through the same resolver TaffyGo's address bar uses, so a
 * search from the system widget gets TaffyGo's own honest answer about having chosen no search
 * engine rather than being dropped or sent somewhere invented.
 *
 * <p><b>No user-visible string lives here, which is why there is no {@code strings.xml} half.</b>
 * The activity is themed {@code @style/LauncherTheme}, which is {@code Theme.BrowserUI.NoDisplay};
 * it draws nothing and finishes inside {@code onCreate}. An intent naming an address TaffyGo will
 * not take opens TaffyGo with no new tab, which is the same thing that happens when a person taps
 * the icon.
 */
public class TaffyInboundIntentActivity extends Activity {

    private static final String TAG = "TaffyInbound";

    /**
     * Chromium's own switch for unparcelling file descriptors out of an inbound intent.
     *
     * <p>False, and not a feature check. {@code ChromeLauncherActivity} reads
     * {@code ChromeFeatureList.sUnparcelIntentFileDescriptors}, which is a native call and
     * therefore unavailable here; more to the point, nothing on this path reads a descriptor out of
     * an intent, so the safe constant is the correct one rather than the convenient one.
     */
    private static final boolean UNPARCEL_FILE_DESCRIPTORS = false;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Upstream's own first move, and for its own reason: an intent from an arbitrary
        // application may carry extras this process cannot unparcel, and reading one before
        // sanitizing it is a top crasher in the wild.
        Intent inbound = IntentUtils.sanitizeIntent(getIntent(), UNPARCEL_FILE_DESCRIPTORS);
        startActivity(browserIntentFor(inbound));
        finish();
    }

    /**
     * The intent TaffyGo hands itself: a new object, naming one class, carrying at most one string.
     *
     * <p>At most one, and never both: an address and a search query are alternatives, so the branch
     * below returns rather than falling through. {@link TaffyInboundIntent} guarantees the same
     * thing on its side, and saying it in both places is what makes a future edit to either one
     * fail loudly rather than produce an intent that means two things.
     *
     * <p>{@code ACTION_MAIN} rather than the caller's action, because the action is a statement
     * about what the caller wanted and this intent is a statement about what TaffyGo decided. The
     * three flags are the ones {@code LaunchIntentDispatcher.dispatchToTabbedActivity} sets for the
     * same hop, and they mean the same three things here: land in TaffyGo's own task, come to the
     * front of it rather than stacking, and stay in Recents afterwards.
     */
    private Intent browserIntentFor(@Nullable Intent inbound) {
        Intent out = new Intent(Intent.ACTION_MAIN, null, this, TaffyBrowserActivity.class);
        out.setFlags(
                Intent.FLAG_ACTIVITY_NEW_TASK
                        | Intent.FLAG_ACTIVITY_CLEAR_TOP
                        | Intent.FLAG_ACTIVITY_RETAIN_IN_RECENTS);
        if (inbound == null) return out;

        TaffyInboundIntent decision =
                TaffyInboundIntent.of(
                        inbound.getAction(),
                        inbound.getDataString(),
                        inbound.getType(),
                        IntentUtils.safeGetStringExtra(inbound, Intent.EXTRA_TEXT),
                        IntentUtils.safeGetStringExtra(inbound, SearchManager.QUERY));
        if (decision.submitsQuery()) {
            out.putExtra(TaffyInboundIntent.EXTRA_QUERY, decision.getQuery());
            return out;
        }
        if (!decision.opensUrl()) {
            Log.i(TAG, "no address TaffyGo will open; opening the browser");
            return out;
        }

        String url = decision.getUrl();
        out.putExtra(TaffyInboundIntent.EXTRA_URL, url);
        carryReadPermissionForContent(out, url);
        return out;
    }

    /**
     * Keeps a {@code content:} document readable after this activity has finished.
     *
     * <p>A URI permission granted to an activity lasts as long as that activity's task, and this
     * activity finishes immediately — so a {@code content:} address handed straight on as a string
     * would arrive at a browser that is no longer allowed to read it. Re-granting it on the
     * outgoing intent is upstream's own answer to exactly this problem
     * ({@code LaunchIntentDispatcher.dispatchToTabbedActivity} does the same two lines for the same
     * scheme), and it is the one reason the outgoing intent carries a data URI at all.
     * {@link TaffyBrowserActivity} still reads the address from the extra and never from the data,
     * so the two cannot disagree about what is opened.
     */
    private static void carryReadPermissionForContent(Intent out, @Nullable String url) {
        if (url == null || !url.regionMatches(true, 0, "content:", 0, "content:".length())) return;
        out.setData(Uri.parse(url));
        out.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    }
}
