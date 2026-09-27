// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import org.chromium.taffy.shell.TaffyDownloadNotification.Action;
import org.chromium.taffy.shell.TaffyDownloadNotification.State;

/**
 * Screen SCR-802's closed content and control rules.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyDownloadNotificationTest {

    @Test
    public void everyStateHasItsOwnTitle() {
        // Four states that read alike would make the notification useless at a
        // glance, which is the only way it is ever read.
        for (State first : State.values()) {
            for (State second : State.values()) {
                if (first != second) {
                    assertNotEquals(
                            first + " and " + second + " share a title",
                            TaffyDownloadNotification.titleFor(first),
                            TaffyDownloadNotification.titleFor(second));
                }
            }
        }
    }

    @Test
    public void aFinishedDownloadOffersNoStaleControl() {
        assertArrayEquals(
                new Action[] {},
                TaffyDownloadNotification.actionsFor(State.COMPLETE, true, true, true));
    }

    @Test
    public void aFailedDownloadOffersOnlyRealResume() {
        assertArrayEquals(
                new Action[] {Action.RESUME},
                TaffyDownloadNotification.actionsFor(State.FAILED, false, true, true));
        assertArrayEquals(
                new Action[] {},
                TaffyDownloadNotification.actionsFor(State.FAILED, false, false, true));
    }

    @Test
    public void workInFlightOffersOnlyCurrentProviderControls() {
        assertArrayEquals(
                new Action[] {Action.PAUSE, Action.CANCEL},
                TaffyDownloadNotification.actionsFor(State.RUNNING, true, false, true));
        assertArrayEquals(
                new Action[] {Action.RESUME, Action.CANCEL},
                TaffyDownloadNotification.actionsFor(State.PAUSED, false, true, true));
        assertArrayEquals(
                new Action[] {Action.CANCEL},
                TaffyDownloadNotification.actionsFor(State.RUNNING, false, false, true));
        assertArrayEquals(
                new Action[] {},
                TaffyDownloadNotification.actionsFor(State.PAUSED, false, false, false));
    }

    @Test
    public void notificationHasNoInventedRetryOpenOrShareAction() {
        assertArrayEquals(
                new Action[] {Action.PAUSE, Action.RESUME, Action.CANCEL}, Action.values());
    }

    @Test
    public void onlyARunningDownloadKeepsSomethingMoving() {
        assertTrue(TaffyDownloadNotification.showsProgress(State.RUNNING));
        assertFalse(TaffyDownloadNotification.showsProgress(State.PAUSED));
        assertFalse(TaffyDownloadNotification.showsProgress(State.COMPLETE));
        assertFalse(TaffyDownloadNotification.showsProgress(State.FAILED));
    }

    @Test
    public void workInFlightCannotBeSwipedAway() {
        // A running download's notification is the only handle on it. Losing it
        // to a swipe would leave the work with no surface at all.
        assertFalse(TaffyDownloadNotification.isDismissable(State.RUNNING));
        assertFalse(TaffyDownloadNotification.isDismissable(State.PAUSED));
        assertTrue(TaffyDownloadNotification.isDismissable(State.COMPLETE));
        assertTrue(TaffyDownloadNotification.isDismissable(State.FAILED));
    }

    @Test
    public void everyActionHasALabel() {
        for (Action action : Action.values()) {
            // Throws on an unhandled member, which is the point: a new action
            // cannot ship with no words on its button.
            TaffyDownloadNotification.labelFor(action);
        }
    }
}
