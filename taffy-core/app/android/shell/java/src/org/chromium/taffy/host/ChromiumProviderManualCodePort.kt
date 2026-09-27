// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.providerauth.ProviderManualCodePort
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.taffy.browser.TaffyProfilePlatformBridge

/**
 * Hands a manually entered code, or the address the vendor's tab landed on,
 * to the browser's provider auth broker (decision 0095 section 2).
 *
 * The sign-in engine calls this from whatever dispatcher the screen's view
 * model ran on; the JNI entry reads the Profile and the broker's own objects,
 * which are UI-thread affine, so the call is moved there and its answer is
 * carried back. The value is handed over whole: the broker owns the shape,
 * every bound on it, and the claim that it belongs to a live flow. Nothing
 * here inspects, splits or logs it.
 */
internal class ChromiumProviderManualCodePort(
    private val profile: Profile,
    private val main: CoroutineDispatcher,
) : ProviderManualCodePort {
    override suspend fun submit(flowId: String, entered: String): Boolean =
        withContext(main) {
            TaffyProfilePlatformBridge.submitProviderAuthCode(profile, flowId, entered)
        }
}
