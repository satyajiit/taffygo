// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import static org.chromium.ui.listmenu.ListMenuItemProperties.ENABLED;
import static org.chromium.ui.listmenu.ListMenuItemProperties.MENU_ITEM_ID;
import static org.chromium.ui.listmenu.ListMenuItemProperties.TITLE;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.blink_public.common.ContextMenuDataMediaType;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.embedder_support.contextmenu.ContextMenuNativeDelegate;
import org.chromium.components.embedder_support.contextmenu.ContextMenuParams;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulator;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.listmenu.MenuModelBridge;
import org.chromium.ui.modelutil.MVCListAdapter.ListItem;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.List;

@RunWith(BaseRobolectricTestRunner.class)
public class TaffyContextMenuPopulatorTest {
    private Context mContext;
    private Tab mTab;
    private TabCreator mCreator;
    private TaffyManualNavigationCoordinator mNavigation;

    @Before
    public void setUp() {
        mContext = mock(Context.class);
        when(mContext.getString(anyInt())).thenAnswer(call -> "title-" + call.getArgument(0));
        mTab = mock(Tab.class);
        TabModelSelector selector = mock(TabModelSelector.class);
        TabCreatorManager manager = mock(TabCreatorManager.class);
        mCreator = mock(TabCreator.class);
        when(selector.getCurrentTab()).thenReturn(mTab);
        when(selector.getTabCreatorManager()).thenReturn(manager);
        when(manager.getTabCreator(false)).thenReturn(mCreator);
        TaffyManualGestureGate gate = new TaffyManualGestureGate(() -> 100L);
        mNavigation =
                new TaffyManualNavigationCoordinator(
                        () -> selector, gate, new TaffyExternalAppHandoff(gate, ignored -> true));
    }

    @Test
    public void linkRowsExposeLocalizedTitlesAndStableActions() {
        ContextMenuPopulator populator =
                populator(
                        mContext,
                        params(
                                ContextMenuDataMediaType.NONE,
                                "https://example.test/path",
                                "Example",
                                ""));

        List<ModelList> groups = populator.buildContextMenu();
        assertEquals(1, groups.size());
        assertEquals(
                List.of(
                        R.id.contextmenu_open_in_new_tab,
                        R.id.contextmenu_copy_link_address,
                        R.id.contextmenu_copy_link_text),
                itemIds(groups.get(0)));
        for (ListItem row : groups.get(0)) {
            assertFalse(row.model.get(TITLE).toString().isEmpty());
            assertTrue(row.model.get(ENABLED));
        }
    }

    @Test
    public void imageAddsOnlySafeOpenImageAction() {
        ContextMenuPopulator populator =
                populator(
                        mContext,
                        params(
                                ContextMenuDataMediaType.IMAGE,
                                "",
                                "",
                                "https://example.test/image.png"));

        List<ModelList> groups = populator.buildContextMenu();
        assertEquals(1, groups.size());
        assertEquals(List.of(R.id.contextmenu_open_image_in_new_tab), itemIds(groups.get(0)));
    }

    @Test
    public void openRowUsesSameProfileBackgroundCreator() {
        when(mCreator.createNewTab(
                        any(LoadUrlParams.class),
                        eq(TabLaunchType.FROM_LONGPRESS_BACKGROUND),
                        same(mTab)))
                .thenReturn(mock(Tab.class));
        ContextMenuPopulator populator =
                populator(
                        mContext,
                        params(ContextMenuDataMediaType.NONE, "https://example.test/", "", ""));

        assertTrue(populator.onItemSelected(R.id.contextmenu_open_in_new_tab));
    }

    @Test
    public void privateClipboardContentIsMarkedSensitive() {
        Context realContext = ApplicationProvider.getApplicationContext();
        when(mTab.isOffTheRecord()).thenReturn(true);
        ContextMenuPopulator populator =
                populator(
                        realContext,
                        params(ContextMenuDataMediaType.NONE, "https://example.test/", "", ""));

        assertTrue(populator.onItemSelected(R.id.contextmenu_copy_link_address));
        ClipboardManager clipboard = realContext.getSystemService(ClipboardManager.class);
        ClipData clip = clipboard.getPrimaryClip();
        assertNotNull(clip);
        assertEquals(
                "https://example.test/", clip.getItemAt(0).coerceToText(realContext).toString());
        assertTrue(
                clip.getDescription().getExtras().getBoolean("android.content.extra.IS_SENSITIVE"));
    }

    @Test
    public void destroyedFactoryProducesNoRows() {
        TaffyContextMenuPopulatorFactory factory =
                new TaffyContextMenuPopulatorFactory(mTab, mNavigation);
        factory.onDestroy();

        assertFalse(factory.isEnabled());
        assertTrue(
                factory.createContextMenuPopulator(
                                mContext,
                                params(
                                        ContextMenuDataMediaType.NONE,
                                        "https://example.test/",
                                        "",
                                        ""),
                                mock(ContextMenuNativeDelegate.class))
                        .buildContextMenu()
                        .isEmpty());
    }

    private ContextMenuPopulator populator(Context context, ContextMenuParams params) {
        return new TaffyContextMenuPopulator(
                context, mTab, params, mNavigation, /* disabled= */ false);
    }

    private static List<Integer> itemIds(ModelList rows) {
        List<Integer> ids = new ArrayList<>(rows.size());
        for (ListItem row : rows) {
            ids.add(row.model.get(MENU_ITEM_ID));
        }
        return ids;
    }

    private static ContextMenuParams params(
            int mediaType, String link, String linkText, String source) {
        GURL linkUrl = TaffyTestGurl.from(link);
        GURL sourceUrl = TaffyTestGurl.from(source);
        return new ContextMenuParams(
                /* nativePtr= */ 0,
                mock(MenuModelBridge.class),
                mediaType,
                /* mediaFlags= */ 0,
                TaffyTestGurl.from("https://page.example/"),
                linkUrl,
                linkText,
                linkUrl,
                sourceUrl,
                /* titleText= */ "",
                /* referrer= */ null,
                /* canSaveMedia= */ false,
                /* triggeringTouchXDp= */ 0,
                /* triggeringTouchYDp= */ 0,
                /* sourceType= */ 0,
                /* openedFromHighlight= */ false,
                /* openedFromInterestFor= */ false,
                /* interestForNodeID= */ 0,
                /* additionalNavigationParams= */ null);
    }
}
