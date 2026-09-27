// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.graphics.Rect;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

/** Recognizes completed manual pointer or keyboard activation inside the visible page surface. */
final class TaffyManualGestureInput {
    private TaffyManualGestureInput() {}

    static boolean isPageGesture(MotionEvent event, View pageView) {
        if (event.getActionMasked() != MotionEvent.ACTION_UP
                || (event.getSource() & InputDevice.SOURCE_CLASS_POINTER) == 0
                || !pageView.isShown()) {
            return false;
        }
        Rect visibleBounds = new Rect();
        return pageView.getGlobalVisibleRect(visibleBounds)
                && visibleBounds.contains((int) event.getRawX(), (int) event.getRawY());
    }

    static boolean isPageKeyActivation(KeyEvent event, View pageView) {
        if (event.getAction() != KeyEvent.ACTION_UP
                || event.isCanceled()
                || !pageView.isShown()
                || !pageView.hasFocus()) {
            return false;
        }
        return switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_ENTER,
                    KeyEvent.KEYCODE_NUMPAD_ENTER,
                    KeyEvent.KEYCODE_DPAD_CENTER ->
                    true;
            default -> false;
        };
    }
}
