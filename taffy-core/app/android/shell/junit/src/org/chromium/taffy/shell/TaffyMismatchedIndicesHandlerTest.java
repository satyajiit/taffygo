// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.RobolectricTestRunner;

/** Checks the destructive half of Chromium's window-id mismatch protocol. */
@RunWith(RobolectricTestRunner.class)
public class TaffyMismatchedIndicesHandlerTest {
    @Mock
    private TaffyBrowserActivity mPrevious;

    private TaffyMismatchedIndicesHandler mHandler;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        mHandler = new TaffyMismatchedIndicesHandler();
    }

    @Test
    public void liveDifferentTaskKeepsItsWindow() {
        when(mPrevious.isFinishing()).thenReturn(false);

        assertFalse(mHandler.handleMismatchedIndices(mPrevious,
                /* isActivityInAppTasks= */ true,
                /* isActivityInSameTask= */ false));

        verify(mPrevious, never()).releaseTabStoreForWindowReassignment();
        verify(mPrevious, never()).finish();
    }

    @Test
    public void sameTaskRecreationReleasesStoreAndFinishesPrevious() {
        when(mPrevious.isFinishing()).thenReturn(false);

        assertTrue(mHandler.handleMismatchedIndices(mPrevious,
                /* isActivityInAppTasks= */ true,
                /* isActivityInSameTask= */ true));

        verify(mPrevious).releaseTabStoreForWindowReassignment();
        verify(mPrevious).finish();
    }

    @Test
    public void finishingActivityReleasesStoreOnlyOnce() {
        when(mPrevious.isFinishing()).thenReturn(true);

        assertTrue(mHandler.handleMismatchedIndices(mPrevious,
                /* isActivityInAppTasks= */ true,
                /* isActivityInSameTask= */ false));

        verify(mPrevious).releaseTabStoreForWindowReassignment();
        verify(mPrevious, never()).finish();
    }

    @Test
    public void managerMayReassignWhenTheRequestedWindowIsLive() {
        assertFalse(mHandler.skipIndexReassignment());
    }
}
