// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;

import java.io.Closeable;

/** Browser-owned registration of one product window's real TabModel selection. */
public final class TaffyTaskSourceSelectionBridge implements Closeable {
    /** Product-window mechanics used only after native policy binds an action. */
    public interface BrowserActions {
        @Nullable
        String resolveSearchAddress(String query);

        @Nullable
        Tab openTaskTab(String taskId, String actionId, String destinationAddress);

        void openTaskDiscoveryTab(String taskId, String effectId, Callback<Tab> callback);

        boolean startSearch(Tab tab, String query, String destinationAddress);

        boolean activateTaskTab(Tab tab);

        boolean closeTaskTab(Tab tab);
    }

    private final Profile mProfile;
    private long mWindowToken;
    private long mBrowserActionsPtr;
    private @Nullable BrowserActions mBrowserActions;

    /** Opens an opaque native window registration. Private profiles are refused. */
    public static TaffyTaskSourceSelectionBridge open(Profile profile) {
        long token = TaffyTaskSourceSelectionBridgeJni.get().registerWindow(profile);
        if (token == 0) {
            throw new IllegalStateException("The task-source window is unavailable");
        }
        return new TaffyTaskSourceSelectionBridge(profile, token);
    }

    private TaffyTaskSourceSelectionBridge(Profile profile, long windowToken) {
        mProfile = profile;
        mWindowToken = windowToken;
    }

    public boolean registerTab(Tab tab) {
        return mWindowToken != 0
                && TaffyTaskSourceSelectionBridgeJni.get()
                        .registerTab(
                                mProfile,
                                mWindowToken,
                                tab,
                                tab.getLaunchType() == TabLaunchType.FROM_RESTORE);
    }

    /** Binds exactly one production window delegate for this registration. */
    public boolean bindBrowserActions(BrowserActions actions) {
        if (mWindowToken == 0 || mBrowserActionsPtr != 0 || actions == null) {
            return false;
        }
        mBrowserActions = actions;
        long ptr =
                TaffyTaskSourceSelectionBridgeJni.get()
                        .initBrowserActions(this, mProfile, mWindowToken);
        if (ptr == 0) {
            mBrowserActions = null;
            return false;
        }
        mBrowserActionsPtr = ptr;
        return true;
    }

    /**
     * Claims task ownership after WebContents creation and before TabModel publication. The
     * dedicated task-tab creator must make this call inside its pre-add hook; a claim after add is
     * deliberately refused by native.
     */
    public boolean claimAssistantCreatedTaskTab(String taskId, String actionId, Tab tab) {
        return mBrowserActionsPtr != 0
                && TaffyTaskSourceSelectionBridgeJni.get()
                        .claimAssistantCreatedTaskTab(
                                mProfile, mWindowToken, taskId, actionId, tab);
    }

    /** Whether native browser provenance marks this exact regular-profile tab as Taffy's. */
    public boolean isAssistantCreatedTaskTab(Tab tab) {
        return mWindowToken != 0
                && tab != null
                && TaffyTaskSourceSelectionBridgeJni.get().isAssistantCreatedTaskTab(mProfile, tab);
    }

    /** Exact creating-task attribution for display; empty never grants a task ownership. */
    public String getCreatingTaskId(Tab tab) {
        return mWindowToken != 0 && tab != null
                ? TaffyTaskSourceSelectionBridgeJni.get().getCreatingTaskId(mProfile, tab)
                : "";
    }

    /** Exact current accepted source membership, separate from creating-task ownership. */
    public boolean isAcceptedTaskSourceTab(String taskId, Tab tab) {
        return mWindowToken != 0 && tab != null && !taskId.isEmpty()
                && TaffyTaskSourceSelectionBridgeJni.get()
                        .isAcceptedTaskSourceTab(mProfile, taskId, tab);
    }

    public void unregisterTab(Tab tab) {
        if (mWindowToken != 0) {
            TaffyTaskSourceSelectionBridgeJni.get().unregisterTab(mProfile, mWindowToken, tab);
        }
    }

    /** Selection is a Tab object from the registered product selector, never a UI identifier. */
    public boolean select(@Nullable Tab tab) {
        if (mWindowToken == 0) return false;
        if (tab == null) {
            TaffyTaskSourceSelectionBridgeJni.get().clearSelection(mProfile, mWindowToken);
            return true;
        }
        return TaffyTaskSourceSelectionBridgeJni.get().select(mProfile, mWindowToken, tab);
    }

    public boolean activate() {
        return mWindowToken != 0
                && TaffyTaskSourceSelectionBridgeJni.get().activate(mProfile, mWindowToken);
    }

    public void deactivate() {
        if (mWindowToken != 0) {
            TaffyTaskSourceSelectionBridgeJni.get().deactivate(mProfile, mWindowToken);
        }
    }

    @Override
    public void close() {
        long token = mWindowToken;
        if (token == 0) return;
        long browserActionsPtr = mBrowserActionsPtr;
        mBrowserActionsPtr = 0;
        mBrowserActions = null;
        if (browserActionsPtr != 0) {
            TaffyTaskSourceSelectionBridgeJni.get()
                    .destroyBrowserActions(mProfile, browserActionsPtr);
        }
        mWindowToken = 0;
        TaffyTaskSourceSelectionBridgeJni.get().unregisterWindow(mProfile, token);
    }

    @CalledByNative
    private @Nullable String resolveSearchAddress(String query) {
        BrowserActions actions = mBrowserActions;
        return actions != null ? actions.resolveSearchAddress(query) : null;
    }

    @CalledByNative
    private @Nullable Tab openTaskTab(String taskId, String actionId, String destinationAddress) {
        BrowserActions actions = mBrowserActions;
        return actions != null ? actions.openTaskTab(taskId, actionId, destinationAddress) : null;
    }

    @CalledByNative
    private void openTaskDiscoveryTab(String taskId, String effectId, long requestId) {
        BrowserActions actions = mBrowserActions;
        if (actions == null) {
            completeTaskDiscoveryTab(requestId, null);
            return;
        }
        actions.openTaskDiscoveryTab(
                taskId, effectId, tab -> completeTaskDiscoveryTab(requestId, tab));
    }

    private void completeTaskDiscoveryTab(long requestId, @Nullable Tab tab) {
        long browserActionsPtr = mBrowserActionsPtr;
        if (browserActionsPtr != 0) {
            TaffyTaskSourceSelectionBridgeJni.get()
                    .completeTaskDiscoveryTab(mProfile, browserActionsPtr, requestId, tab);
        }
    }

    @CalledByNative
    private boolean startSearch(Tab tab, String query, String destinationAddress) {
        BrowserActions actions = mBrowserActions;
        return actions != null && actions.startSearch(tab, query, destinationAddress);
    }

    @CalledByNative
    private boolean activateTaskTab(Tab tab) {
        BrowserActions actions = mBrowserActions;
        return actions != null && actions.activateTaskTab(tab);
    }

    @CalledByNative
    private boolean closeTaskTab(Tab tab) {
        BrowserActions actions = mBrowserActions;
        return actions != null && actions.closeTaskTab(tab);
    }

    @NativeMethods
    interface Natives {
        boolean isAcceptedTaskSourceTab(
                @JniType("Profile*") Profile profile,
                @JniType("std::string") String taskId,
                @JniType("TabAndroid*") Tab tab);
        long registerWindow(@JniType("Profile*") Profile profile);

        void unregisterWindow(@JniType("Profile*") Profile profile, long windowToken);

        boolean registerTab(
                @JniType("Profile*") Profile profile,
                long windowToken,
                @JniType("TabAndroid*") Tab tab,
                boolean sessionRestored);

        void unregisterTab(
                @JniType("Profile*") Profile profile,
                long windowToken,
                @JniType("TabAndroid*") Tab tab);

        boolean select(
                @JniType("Profile*") Profile profile,
                long windowToken,
                @JniType("TabAndroid*") Tab tab);

        void clearSelection(@JniType("Profile*") Profile profile, long windowToken);

        boolean activate(@JniType("Profile*") Profile profile, long windowToken);

        void deactivate(@JniType("Profile*") Profile profile, long windowToken);

        long initBrowserActions(
                TaffyTaskSourceSelectionBridge caller,
                @JniType("Profile*") Profile profile,
                long windowToken);

        void destroyBrowserActions(@JniType("Profile*") Profile profile, long browserActionsPtr);

        void completeTaskDiscoveryTab(
                @JniType("Profile*") Profile profile,
                long browserActionsPtr,
                long requestId,
                @JniType("TabAndroid*") @Nullable Tab tab);

        boolean claimAssistantCreatedTaskTab(
                @JniType("Profile*") Profile profile,
                long windowToken,
                @JniType("std::string") String taskId,
                @JniType("std::string") String actionId,
                @JniType("TabAndroid*") Tab tab);

        boolean isAssistantCreatedTaskTab(
                @JniType("Profile*") Profile profile, @JniType("TabAndroid*") Tab tab);

        @JniType("std::string")
        String getCreatingTaskId(
                @JniType("Profile*") Profile profile, @JniType("TabAndroid*") Tab tab);
    }
}
