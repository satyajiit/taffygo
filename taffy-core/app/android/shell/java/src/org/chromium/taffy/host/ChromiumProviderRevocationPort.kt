// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.ProviderRevocationPort
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.taffy.browser.TaffyProfilePlatformBridge

/**
 * Hands a signed-out subscription's token to the browser's provider auth
 * broker for vendor-side revocation (decision 0081).
 *
 * The coordinator calls this from inside its per-provider section, on
 * whatever dispatcher the sign-out ran on; the JNI entry reads the Profile
 * and the browser's own objects, which are UI-thread affine, so the call is
 * posted there. Fire-and-forget by contract: the token String below is a
 * copy the port makes for the JNI boundary, and the caller zeroes its own
 * bytes the moment this returns.
 */
internal class ChromiumProviderRevocationPort(
    private val profile: Profile,
) : ProviderRevocationPort {
    override fun revokeBestEffort(providerId: String, token: ByteArray) {
        val copied = token.decodeToString()
        ThreadUtils.runOnUiThread {
            TaffyProfilePlatformBridge.revokeProviderCredential(profile, providerId, copied)
        }
    }
}
