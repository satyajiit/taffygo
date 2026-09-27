// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.chromium.ui.listmenu.ListMenuItemProperties.ENABLED;
import static org.chromium.ui.listmenu.ListMenuItemProperties.MENU_ITEM_ID;
import static org.chromium.ui.listmenu.ListMenuItemProperties.TEXT_APPEARANCE_ID;
import static org.chromium.ui.listmenu.ListMenuItemProperties.TITLE;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.PersistableBundle;

import androidx.annotation.Nullable;
import androidx.annotation.StringRes;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.browser_ui.widget.BrowserUiListMenuUtils;
import org.chromium.components.embedder_support.contextmenu.ChipDelegate;
import org.chromium.components.embedder_support.contextmenu.ContextMenuParams;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulator;
import org.chromium.ui.listmenu.ListItemType;
import org.chromium.ui.listmenu.ListMenuItemProperties;
import org.chromium.ui.modelutil.MVCListAdapter.ListItem;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.List;

/** A bounded link-and-image context menu with no share, download, or external-app side path. */
final class TaffyContextMenuPopulator implements ContextMenuPopulator {
    private static final String SENSITIVE_CLIPBOARD_EXTRA = "android.content.extra.IS_SENSITIVE";

    private final Context mContext;
    private final Tab mTab;
    private final ContextMenuParams mParams;
    private final TaffyManualNavigationCoordinator mNavigation;
    private final boolean mDisabled;

    TaffyContextMenuPopulator(
            Context context,
            Tab tab,
            ContextMenuParams params,
            TaffyManualNavigationCoordinator navigation,
            boolean disabled) {
        mContext = context;
        mTab = tab;
        mParams = params;
        mNavigation = navigation;
        mDisabled = disabled;
    }

    @Override
    public List<ModelList> buildContextMenu() {
        List<ModelList> groups = new ArrayList<>();
        if (mDisabled || mTab.isDestroyed() || mTab.isClosing()) return groups;

        ModelList linkItems = new ModelList();
        if (mParams.isAnchor()) {
            if (TaffyManualNavigationCoordinator.isSafeWebAddress(mParams.getLinkUrl())) {
                linkItems.add(
                        item(
                                R.id.contextmenu_open_in_new_tab,
                                R.string.contextmenu_open_in_new_tab));
            }
            if (!mParams.getUnfilteredLinkUrl().isEmpty()) {
                linkItems.add(
                        item(
                                R.id.contextmenu_copy_link_address,
                                R.string.contextmenu_copy_link_address));
            }
            if (!mParams.getLinkText().trim().isEmpty()) {
                linkItems.add(
                        item(R.id.contextmenu_copy_link_text, R.string.contextmenu_copy_link_text));
            }
        }
        if (!linkItems.isEmpty()) groups.add(linkItems);

        ModelList imageItems = new ModelList();
        if (mParams.isImage()
                && TaffyManualNavigationCoordinator.isSafeWebAddress(mParams.getSrcUrl())) {
            imageItems.add(
                    item(
                            R.id.contextmenu_open_image_in_new_tab,
                            R.string.contextmenu_open_image_in_new_tab));
        }
        if (!imageItems.isEmpty()) groups.add(imageItems);
        return groups;
    }

    @Override
    public boolean onItemSelected(int itemId) {
        if (itemId == R.id.contextmenu_open_in_new_tab) {
            return mNavigation.openContextLink(mTab, mParams.getLinkUrl());
        }
        if (itemId == R.id.contextmenu_open_image_in_new_tab) {
            return mNavigation.openContextLink(mTab, mParams.getSrcUrl());
        }
        if (itemId == R.id.contextmenu_copy_link_address) {
            return copy(mParams.getUnfilteredLinkUrl().getSpec());
        }
        if (itemId == R.id.contextmenu_copy_link_text) {
            return copy(mParams.getLinkText());
        }
        return false;
    }

    @Override
    public void onMenuClosed() {}

    @Override
    public boolean isIncognito() {
        return mTab.isOffTheRecord();
    }

    @Override
    public String getPageTitle() {
        String title = mTab.getTitle();
        return title == null ? "" : title;
    }

    @Override
    public @Nullable ChipDelegate getChipDelegate() {
        return null;
    }

    private ListItem item(int id, @StringRes int title) {
        PropertyModel model =
                new PropertyModel.Builder(ListMenuItemProperties.ALL_KEYS)
                        .with(MENU_ITEM_ID, id)
                        .with(TITLE, mContext.getString(title))
                        .with(ENABLED, true)
                        .with(
                                TEXT_APPEARANCE_ID,
                                BrowserUiListMenuUtils.getDefaultTextAppearanceStyle())
                        .build();
        return new ListItem(ListItemType.MENU_ITEM, model);
    }

    private boolean copy(String text) {
        if (text.isEmpty()) return false;
        ClipboardManager clipboard = mContext.getSystemService(ClipboardManager.class);
        if (clipboard == null) return false;
        ClipData clip = ClipData.newPlainText(/* label= */ "", text);
        if (mTab.isOffTheRecord()) {
            PersistableBundle extras = new PersistableBundle();
            extras.putBoolean(SENSITIVE_CLIPBOARD_EXTRA, true);
            clip.getDescription().setExtras(extras);
        }
        clipboard.setPrimaryClip(clip);
        return true;
    }
}
