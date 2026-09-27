// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.when;

import kotlinx.coroutines.Dispatchers;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

/** Tests the regular-only platform-port construction boundary. */
@RunWith(BaseRobolectricTestRunner.class)
public final class ChromiumProfilePortsTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Profile mPrivateProfile;

    @Test
    public void offTheRecordProfileIsRejectedBeforePortsAreConstructed() {
        when(mPrivateProfile.isOffTheRecord()).thenReturn(true);

        assertThrows(
                IllegalStateException.class,
                () -> new ChromiumProfilePorts(mPrivateProfile, Dispatchers.getUnconfined()));
    }
}
