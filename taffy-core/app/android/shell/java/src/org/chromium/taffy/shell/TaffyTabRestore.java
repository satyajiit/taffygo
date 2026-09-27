// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;

/**
 * The two choices restoring a tab makes that are choices rather than calls.
 *
 * <p>{@link TaffyTabCreator} rebuilds a saved tab itself, so the small decisions upstream makes
 * inside {@code TabImpl.restoreFieldsFromState} and {@code TabImpl.unfreezeContents} are made here
 * instead. Both are pure functions over what was on disk, and both fail silently when they are
 * wrong — a tab that quietly loses its group, or a corrupt session that lands somewhere nobody
 * chose — so both are separated to where a host test can call them.
 *
 * <p>Neither of them is TaffyGo inventing behaviour. Each mirrors the upstream line it replaces,
 * and the documentation on each says which line that is, so that a milestone rebase which changes
 * upstream's answer changes a comparison a reader can make rather than one nobody remembers to
 * make.
 */
public final class TaffyTabRestore {

    private TaffyTabRestore() {}

    /**
     * The group root a restored tab belongs to.
     *
     * <p>Upstream: {@code setRootId(state.rootId == Tab.INVALID_TAB_ID ? mId : state.rootId)} in
     * {@code TabImpl.restoreFieldsFromState}. A tab that was in no group has no saved root, and a
     * tab whose root is itself is what "no group" means in this model — so the substitution is what
     * keeps an ungrouped tab ungrouped instead of joining it to whatever tab happens to hold the
     * invalid id.
     *
     * @param savedRootId the root id read from the tab's saved state.
     * @param tabId the id this tab is being restored under.
     */
    public static int rootIdFor(int savedRootId, int tabId) {
        return savedRootId == Tab.INVALID_TAB_ID ? tabId : savedRootId;
    }

    /**
     * Whether a new tab is the persistent store's last-resort restoration path.
     *
     * <p>{@code ChromeTabCreator} normally creates a background tab lazily on a low-memory device.
     * A lazy tab cannot ever be inflated in {@code TaffyBrowserActivity}, because upstream's load
     * path requires a {@code ChromeActivity}. Restoration therefore has to create a live tab even
     * when the saved state file was unreadable and the store falls back to {@code createNewTab}.
     */
    public static boolean requiresLiveNewTab(@TabLaunchType int launchType) {
        return launchType == TabLaunchType.FROM_RESTORE;
    }

    /**
     * Where a tab goes when its saved navigation history cannot be read back at all.
     *
     * <p>Upstream: the three-way choice in {@code TabImpl.unfreezeContents}, which prefers the
     * virtual URL held in the saved state, falls back to the fallback URL the persistent store
     * writes onto that state ({@code TabPersistentStoreImpl.restoreTab} calls
     * {@code setFallbackUrlForRestorationFailure(tabToRestore.url)}), and otherwise loads the
     * new-tab page. The order is upstream's and is kept.
     *
     * <p>The last step is the one that differs, and it has to: TaffyGo resolves no native page —
     * {@link TaffyTabDelegateFactory#createNativePage} returns null by design — so
     * {@code chrome://newtab} would be fetched as an ordinary web page and answered with an error.
     * The start page is passed in rather than named here, because the address a tab opens on when
     * nothing else says otherwise is {@code TaffyBrowserActivity}'s single statement of it and a
     * second copy would be a second answer.
     *
     * @param virtualUrl the address recorded in the saved state, if it could be read.
     * @param savedFallbackUrl the address the persistent store recorded beside the state.
     * @param startPageUrl the address a TaffyGo tab opens on when nothing else says otherwise.
     */
    public static String fallbackUrlFor(
            @Nullable String virtualUrl,
            @Nullable String savedFallbackUrl,
            String startPageUrl) {
        if (virtualUrl != null && !virtualUrl.isEmpty()) return virtualUrl;
        if (savedFallbackUrl != null && !savedFallbackUrl.isEmpty()) return savedFallbackUrl;
        return startPageUrl;
    }
}
