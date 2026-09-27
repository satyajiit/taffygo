// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabwindow.TabWindowManager;

/** Window separation and restoration rules before Chromium performs final assignment. */
@RunWith(RobolectricTestRunner.class)
public class TaffyWindowIdAllocatorTest {
    @Test
    public void newWindowUsesFirstUnassignedPersistenceId() {
        TabWindowManager manager = mock(TabWindowManager.class);
        when(manager.getTabModelSelectorById(0)).thenReturn(mock(TabModelSelector.class));
        when(manager.getTabModelSelectorById(1)).thenReturn(mock(TabModelSelector.class));

        assertEquals(2, TaffyWindowIdAllocator.requestedId(null, manager, 5));
    }

    @Test
    public void rotationRequestsItsSavedIdForMismatchHandoff() {
        TabWindowManager manager = mock(TabWindowManager.class);
        when(manager.getTabModelSelectorById(3)).thenReturn(mock(TabModelSelector.class));

        assertEquals(3, TaffyWindowIdAllocator.requestedId(3, manager, 5));
    }

    @Test
    public void reachingChromiumWindowLimitRefusesInsteadOfSharingState() {
        TabWindowManager manager = mock(TabWindowManager.class);
        when(manager.getTabModelSelectorById(0)).thenReturn(mock(TabModelSelector.class));
        when(manager.getTabModelSelectorById(1)).thenReturn(mock(TabModelSelector.class));

        assertEquals(TabWindowManager.INVALID_WINDOW_ID,
                TaffyWindowIdAllocator.requestedId(null, manager, 2));
    }
}
