// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import org.chromium.url.GURL;

import java.net.URI;

/** Builds native-free URL values for the shell's host-only policy tests. */
@SuppressWarnings("DoNotMock")
final class TaffyTestGurl {
    private TaffyTestGurl() {}

    static GURL from(String spec) {
        URI parsed = URI.create(spec);
        String scheme = parsed.getScheme() == null ? "" : parsed.getScheme();
        String[] credentials = credentials(parsed.getRawUserInfo());
        GURL result = mock(GURL.class);
        when(result.isEmpty()).thenReturn(spec.isEmpty());
        when(result.isValid()).thenReturn(!spec.isEmpty() && !scheme.isEmpty());
        when(result.getSpec()).thenReturn(spec);
        when(result.getPossiblyInvalidSpec()).thenReturn(spec);
        when(result.getScheme()).thenReturn(scheme);
        when(result.getUsername()).thenReturn(credentials[0]);
        when(result.getPassword()).thenReturn(credentials[1]);
        return result;
    }

    private static String[] credentials(String userInfo) {
        if (userInfo == null || userInfo.isEmpty()) return new String[] {"", ""};
        int separator = userInfo.indexOf(':');
        if (separator < 0) return new String[] {userInfo, ""};
        return new String[] {userInfo.substring(0, separator), userInfo.substring(separator + 1)};
    }
}
