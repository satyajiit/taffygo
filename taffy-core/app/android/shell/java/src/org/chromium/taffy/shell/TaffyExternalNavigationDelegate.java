// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;

import androidx.annotation.Nullable;

import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.Callback;
import org.chromium.base.IntentUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.components.external_intents.ExternalNavigationDelegate;
import org.chromium.components.external_intents.ExternalNavigationHelper;
import org.chromium.components.external_intents.ExternalNavigationParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.url.GURL;

import java.util.List;
import java.util.function.Supplier;

/**
 * The closed upstream-policy delegate behind TaffyGo's manual external-app route.
 *
 * <p>{@link TaffyExternalNavigationHandler} performs the only allowed launch itself: a sanitized
 * external protocol, from the selected regular tab, backed by both renderer activation and a fresh
 * one-use Android page gesture, through Android's chooser. It deliberately never calls {@code
 * super.shouldOverrideUrlLoading}. The answers below keep every inherited Chrome launch, fallback,
 * referrer, trusted-caller, private-tab and close-after-launch route shut if that design changes
 * accidentally.
 *
 * <p>This is not Chrome's {@code ExternalNavigationDelegateImpl}. Importing that implementation
 * would silently adopt Chrome's scheme, Play Store, WebAPK, trusted-caller and fallback policy.
 * TaffyGo's bounded route lives in {@link TaffyExternalAppHandoff}; this object supplies only tab
 * facts and explicit refusals required by the superclass constructor.
 */
class TaffyExternalNavigationDelegate implements ExternalNavigationDelegate {

    private final Tab mTab;

    TaffyExternalNavigationDelegate(Tab tab) {
        mTab = tab;
    }

    // -----------------------------------------------------------------------
    // Facts about this tab. Each one is read from the tab and nowhere else.
    // -----------------------------------------------------------------------

    @Override
    public @Nullable Context getContext() {
        WindowAndroid window = mTab.getWindowAndroid();
        return window == null ? null : window.getContext().get();
    }

    @Override
    public @Nullable WindowAndroid getWindowAndroid() {
        return mTab.getWindowAndroid();
    }

    @Override
    public @Nullable WebContents getWebContents() {
        return mTab.getWebContents();
    }

    @Override
    public boolean hasValidTab() {
        return !mTab.isDestroyed();
    }

    @Override
    public boolean isIncognito() {
        return mTab.isOffTheRecord();
    }

    @Override
    public boolean isApplicationInForeground() {
        return ApplicationStatus.getStateForApplication()
                == ApplicationState.HAS_RUNNING_ACTIVITIES;
    }

    /**
     * Whether this tab exists because a link asked for a new foreground tab.
     *
     * <p>Read by {@code ExternalNavigationHandler.shouldReparentTab}, which is the one method of
     * that class TaffyGo does not override — so this answer is live rather than theoretical, and it
     * is read from the tab's own launch type rather than assumed.
     */
    @Override
    public boolean wasTabLaunchedFromLinkCreatingNewForegroundTab() {
        return mTab.getLaunchType() == TabLaunchType.FROM_LONGPRESS_FOREGROUND
                || mTab.getLaunchType() == TabLaunchType.FROM_LONGPRESS_FOREGROUND_IN_GROUP;
    }

    @Override
    public boolean wasTabLaunchedFromLinkCreatingNewWindow() {
        return mTab.getLaunchType() == TabLaunchType.FROM_LINK_CREATING_NEW_WINDOW;
    }

    /**
     * Whether TaffyGo itself would handle this intent.
     *
     * <p>Only an intent addressed to this package can be answered without asking the package
     * manager. TaffyGo's manual route never consults this inherited hook: it rebuilds the target,
     * strips explicit packages, and launches the system chooser. Keeping the self answer here
     * prevents an accidentally inherited path from handing the browser back to itself.
     */
    @Override
    public boolean willAppHandleIntent(Intent intent) {
        return IntentUtils.intentTargetsSelf(intent);
    }

    // -----------------------------------------------------------------------
    // The refusals. They keep every inherited route shut; the bounded manual route bypasses super.
    // -----------------------------------------------------------------------

    /**
     * Chrome's inherited external-intent decision remains disabled.
     *
     * <p>It is also the guard in front of both of upstream's external-intent dialogs: every use of
     * {@code ExternalNavigationHandler}'s {@code mModalDialogManager} — the leaving-incognito dialog
     * and the digital-credentials warning — sits after this check. The window does have a dialog
     * manager now, but only for what a page asks for ({@link TaffyPageDialogs}); these two
     * questions are not a page's, and they stay unasked.
     */
    @Override
    public boolean shouldDisableAllExternalIntents() {
        return true;
    }

    /** The same defense per URL, at the earliest point upstream offers it. */
    @Override
    public boolean shouldDisableExternalIntentRequestsForUrl(
            ExternalNavigationParams params, Intent intent) {
        return true;
    }

    /**
     * A tab is never closed because an intent was launched.
     *
     * <p>This is load-bearing rather than tidy. True here reaches {@code
     * InterceptNavigationDelegateClientImpl.closeTab()}, whose first statement is {@code
     * assumeNonNull(mTab.getActivity())} — a compile-time assertion with an empty body at runtime,
     * over a value that is null for every tab in this activity. Tab closure in TaffyGo is decided
     * in exactly one place, {@code ChromiumBrowserMediator.closeTab}, and this is not it.
     */
    @Override
    public boolean canCloseTabOnIntentLaunch() {
        return false;
    }

    /** Nothing to do: the only caller is the intent-launch path, which cannot happen here. */
    @Override
    public void closeTab() {}

    /**
     * TaffyGo presents the leaving-incognito question itself, and its answer is no.
     *
     * <p>True rather than false. False hands the question to upstream's own dialog, which would
     * be shown through the window's dialog manager — the one that exists for a page's own dialogs
     * ({@link TaffyPageDialogs}), not for this. True routes it to {@link
     * #presentLeavingIncognitoModalDialog} below, which answers without a dialog — the honest
     * reading of "TaffyGo's dialogs are a separate transfer".
     */
    @Override
    public boolean hasCustomLeavingIncognitoDialog() {
        return true;
    }

    /**
     * The answer to a question that was never asked, which is no.
     *
     * <p>A private tab must not leak into another application, and TaffyGo has no surface here to
     * ask the person with. Consenting on their behalf is the one answer that would be wrong.
     */
    @Override
    public void presentLeavingIncognitoModalDialog(Callback<Boolean> onUserDecision) {
        onUserDecision.onResult(false);
    }

    /**
     * Avoid upstream's disambiguation dialog because the bounded route always opens Android's
     * chooser itself, after its physical-gesture and sanitization checks.
     */
    @Override
    public boolean shouldAvoidDisambiguationDialog(GURL intentDataUrl) {
        return true;
    }

    /**
     * TaffyGo was not launched by an application it trusts.
     *
     * <p>There is no custom-tab path in this build and no caller identity to check one against, so
     * there is no app whose intent may be given a package name on trust.
     */
    @Override
    public boolean isForTrustedCallingApp(Supplier<List<ResolveInfo>> resolveInfoSupplier) {
        return false;
    }

    /** Unreachable: upstream calls this only when {@link #isForTrustedCallingApp} is true. */
    @Override
    public void setPackageForTrustedCallingApp(Intent intent) {}

    /** TaffyGo neither installs nor launches WebAPKs, so an initial intent never becomes one. */
    @Override
    public boolean shouldLaunchWebApksOnInitialIntent() {
        return false;
    }

    /** One window. Multi-instance is not a capability this build has. */
    @Override
    public boolean shouldLaunchNewWindow(ExternalNavigationParams params) {
        return false;
    }

    /** One task, for the same reason. */
    @Override
    public boolean shouldSelfNavigationLaunchAsMultipleTask(ExternalNavigationParams params) {
        return false;
    }

    /** TaffyGo is never started for a result, so no navigation can be returned as one. */
    @Override
    public boolean shouldReturnAsActivityResult(GURL url) {
        return false;
    }

    /** Unreachable: upstream calls this only when {@link #shouldReturnAsActivityResult} is true. */
    @Override
    public void returnAsActivityResult(GURL url) {
        throw new UnsupportedOperationException(
                "TaffyGo is never started for a result; shouldReturnAsActivityResult() is false");
    }

    /**
     * TaffyGo registers no scheme that starts the browser without an explicit intent.
     *
     * <p>Chrome's answer here is {@code googlechrome://}. Whether TaffyGo ever wants such a scheme
     * is a product decision nobody has made, and inventing one in a delegate would make it by
     * accident.
     */
    @Override
    public @Nullable String getSelfScheme() {
        return null;
    }

    /**
     * Upstream's open-in-app affordance for ordinary web addresses, which TaffyGo has not adopted.
     *
     * <p>False is the literal truth rather than a refusal: the feature this asks about is Chrome's
     * {@code OpenInAppUtils}, which is not part of this product. The refusal lives in {@link
     * #shouldDisableAllExternalIntents}, where it can be read.
     */
    @Override
    public boolean allowExternalNavigationForHttpProtocols(GURL url) {
        return false;
    }

    // -----------------------------------------------------------------------
    // The inherited route sends no intent. The bounded route rebuilds a target with no metadata.
    // -----------------------------------------------------------------------

    /** One window, so there is no window id an intent could carry back. */
    @Override
    public void maybeSetWindowId(Intent intent) {}

    /**
     * No referrer is written into an intent.
     *
     * <p>Upstream records it so the browser can read it back when the intent returns. Nothing here
     * sends one, so this would put a page address into an Android extra for a journey that never
     * happens — a page address leaving this process for no purpose at all.
     */
    @Override
    public void maybeSetPendingReferrer(Intent intent, GURL referrerUrl) {}

    /** Nor the gesture and initiator metadata, for the same reason. */
    @Override
    public void maybeSetRequestMetadata(
            Intent intent, boolean hasUserGesture, boolean isRendererInitiated) {}

    /**
     * Nor a private tab's address, which is the one that must never be written anywhere.
     *
     * <p>Even when TaffyGo does route intents, this stays empty until there is a decision record
     * saying what a private tab may hand to another application.
     */
    @Override
    public void maybeSetPendingIncognitoUrl(Intent intent) {}

    /** There are no custom tabs in this build, so there is no form submission to record against. */
    @Override
    public void notifyCctPasswordSavingRecorderOfExternalNavigation() {}

    /**
     * The inherited route launches nothing, so it has no launch to report. The manual route never
     * passes an intent through this delegate.
     */
    @Override
    public void reportIntentToSafeBrowsing(Intent intent) {}

    /**
     * There is no browser-internal page a private tab has to be kept out of.
     *
     * <p>Upstream's use is {@code chrome://extensions}, which this product does not have.
     */
    @Override
    public @Nullable Intent createIntentToPreventIncognitoAccess(GURL url) {
        return null;
    }

    /**
     * The handler hands itself back so this delegate could invoke the inherited launch machinery.
     * Keeping no reference makes the separate, bounded chooser adapter the only launch owner.
     */
    @Override
    public void setExternalNavigationHelper(ExternalNavigationHelper helper) {}
}
