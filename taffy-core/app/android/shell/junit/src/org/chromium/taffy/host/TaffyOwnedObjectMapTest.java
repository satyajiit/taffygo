// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.fail;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.io.Closeable;

/** Scope-topology tests independent of Android Activity scheduling. */
@RunWith(RobolectricTestRunner.class)
public class TaffyOwnedObjectMapTest {
    @Test
    public void movingTabBetweenWindowsKeepsProfileOwner() throws Exception {
        Object tab = new Object();
        TaffyOwnedObjectMap<Object, RecordingOwner> profileTabs = new TaffyOwnedObjectMap<>();

        RecordingOwner fromFirstWindow =
                profileTabs.getOrCreate(tab, ignored -> new RecordingOwner());
        RecordingOwner fromSecondWindow =
                profileTabs.getOrCreate(tab, ignored -> new RecordingOwner());

        assertSame(fromFirstWindow, fromSecondWindow);
        assertEquals(0, fromFirstWindow.closeCount);
        profileTabs.close();
        assertEquals(1, fromFirstWindow.closeCount);
    }

    @Test
    public void rotationClosesWindowWithoutClosingProfileTab() throws Exception {
        Object tab = new Object();
        Object firstActivity = new Object();
        Object secondActivity = new Object();
        TaffyOwnedObjectMap<Object, RecordingOwner> profileTabs = new TaffyOwnedObjectMap<>();
        TaffyOwnedObjectMap<Object, RecordingOwner> windows = new TaffyOwnedObjectMap<>();
        RecordingOwner tabOwner = profileTabs.getOrCreate(tab, ignored -> new RecordingOwner());
        RecordingOwner firstWindow =
                windows.getOrCreate(firstActivity, ignored -> new RecordingOwner());

        windows.close();
        assertEquals(1, firstWindow.closeCount);
        assertEquals(0, tabOwner.closeCount);

        TaffyOwnedObjectMap<Object, RecordingOwner> recreatedWindows = new TaffyOwnedObjectMap<>();
        recreatedWindows.getOrCreate(secondActivity, ignored -> new RecordingOwner());
        assertSame(tabOwner, profileTabs.getOrCreate(tab, ignored -> new RecordingOwner()));
    }

    @Test
    public void regularAndPrivateProfilesNeverShareOwner() {
        Object tabIdentity = new Object();
        TaffyOwnedObjectMap<Object, RecordingOwner> regular = new TaffyOwnedObjectMap<>();
        TaffyOwnedObjectMap<Object, RecordingOwner> privateProfile = new TaffyOwnedObjectMap<>();

        assertNotSame(regular.getOrCreate(tabIdentity, ignored -> new RecordingOwner()),
                privateProfile.getOrCreate(tabIdentity, ignored -> new RecordingOwner()));
    }

    @Test
    public void profileShutdownClosesEveryOwnerExactlyOnce() throws Exception {
        TaffyOwnedObjectMap<Object, RecordingOwner> owners = new TaffyOwnedObjectMap<>();
        RecordingOwner first = owners.getOrCreate(new Object(), ignored -> new RecordingOwner());
        RecordingOwner second = owners.getOrCreate(new Object(), ignored -> new RecordingOwner());

        owners.close();
        owners.close();

        assertEquals(1, first.closeCount);
        assertEquals(1, second.closeCount);
        assertEquals(0, owners.sizeForTesting());
    }

    @Test
    public void oneBrokenOwnerCannotStrandTheOthers() throws Exception {
        TaffyOwnedObjectMap<Object, Closeable> owners = new TaffyOwnedObjectMap<>();
        RecordingOwner first = new RecordingOwner();
        RecordingOwner second = new RecordingOwner();
        owners.getOrCreate(new Object(), ignored -> first);
        owners.getOrCreate(new Object(), ignored -> second);
        owners.getOrCreate(
                new Object(),
                ignored ->
                        () -> {
                            throw new IllegalStateException("owner close failed");
                        });

        try {
            owners.close();
            fail("A failed close must remain visible");
        } catch (IllegalStateException expected) {
            assertEquals("owner close failed", expected.getMessage());
        }

        assertEquals(1, first.closeCount);
        assertEquals(1, second.closeCount);
        assertEquals(0, owners.sizeForTesting());
        owners.close();
    }

    private static final class RecordingOwner implements Closeable {
        int closeCount;

        @Override
        public void close() {
            closeCount++;
        }
    }
}
