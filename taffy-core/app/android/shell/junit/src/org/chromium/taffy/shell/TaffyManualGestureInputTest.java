// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.graphics.Rect;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

@RunWith(BaseRobolectricTestRunner.class)
public class TaffyManualGestureInputTest {
    private MotionEvent mEvent;
    private View mPage;

    @Before
    public void setUp() {
        mEvent = mock(MotionEvent.class);
        mPage = mock(View.class);
        when(mEvent.getActionMasked()).thenReturn(MotionEvent.ACTION_UP);
        when(mEvent.getSource()).thenReturn(InputDevice.SOURCE_TOUCHSCREEN);
        when(mEvent.getRawX()).thenReturn(50f);
        when(mEvent.getRawY()).thenReturn(75f);
        when(mPage.isShown()).thenReturn(true);
        doAnswer(
                        invocation -> {
                            Rect bounds = invocation.getArgument(0);
                            bounds.set(0, 0, 100, 100);
                            return true;
                        })
                .when(mPage)
                .getGlobalVisibleRect(org.mockito.ArgumentMatchers.any(Rect.class));
    }

    @Test
    public void completedPhysicalPointerInsideVisiblePageQualifies() {
        assertTrue(TaffyManualGestureInput.isPageGesture(mEvent, mPage));
    }

    @Test
    public void moveNonPointerAndOutsideEventsDoNotQualify() {
        when(mEvent.getActionMasked()).thenReturn(MotionEvent.ACTION_MOVE);
        assertFalse(TaffyManualGestureInput.isPageGesture(mEvent, mPage));

        when(mEvent.getActionMasked()).thenReturn(MotionEvent.ACTION_UP);
        when(mEvent.getSource()).thenReturn(InputDevice.SOURCE_KEYBOARD);
        assertFalse(TaffyManualGestureInput.isPageGesture(mEvent, mPage));

        when(mEvent.getSource()).thenReturn(InputDevice.SOURCE_TOUCHSCREEN);
        when(mEvent.getRawX()).thenReturn(101f);
        assertFalse(TaffyManualGestureInput.isPageGesture(mEvent, mPage));
    }

    @Test
    public void hiddenPageDoesNotMintProof() {
        when(mPage.isShown()).thenReturn(false);
        assertFalse(TaffyManualGestureInput.isPageGesture(mEvent, mPage));
    }

    @Test
    public void completedActivationKeyRequiresVisiblePageFocus() {
        KeyEvent enter = new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER);
        when(mPage.hasFocus()).thenReturn(true);

        assertTrue(TaffyManualGestureInput.isPageKeyActivation(enter, mPage));
        assertTrue(
                TaffyManualGestureInput.isPageKeyActivation(
                        new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DPAD_CENTER), mPage));

        when(mPage.hasFocus()).thenReturn(false);
        assertFalse(TaffyManualGestureInput.isPageKeyActivation(enter, mPage));
    }

    @Test
    public void keyDownAndUnrelatedKeyDoNotMintProof() {
        when(mPage.hasFocus()).thenReturn(true);

        assertFalse(
                TaffyManualGestureInput.isPageKeyActivation(
                        new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER), mPage));
        assertFalse(
                TaffyManualGestureInput.isPageKeyActivation(
                        new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK), mPage));
    }
}
