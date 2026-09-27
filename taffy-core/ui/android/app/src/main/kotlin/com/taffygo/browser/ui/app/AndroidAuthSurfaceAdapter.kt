// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.browser.ErrandPagePort
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import java.net.URI
import javax.inject.Inject
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Opens the browser-brokered authorization URL on the product's own surface.
 *
 * ## What this used to do, and why it was wrong
 *
 * It built an Android `CustomTabsIntent` and pinned it to this application. The
 * pinning was, and remains, a correctness rule rather than a preference: one
 * compiled vendor's sign-in is answered by intercepting the navigation its
 * authorization redirects to, before it connects (decision 0095 section 2), and
 * the throttle that does the intercepting is registered in *this* browser
 * process. Left unpinned, Android resolves the tab to whichever browser the
 * person has set as their default, the redirect happens over there where
 * nothing is watching for it, and the flow sits waiting until its ten-minute
 * deadline — which reports as a timeout rather than as the misrouted tab it was.
 * Handing an authorization code to another browser is also not a thing to do
 * quietly.
 *
 * The pinning worked. What it produced did not. This product does not implement
 * the Custom Tabs protocol inbound — `TaffyInboundIntentActivity` says so in as
 * many words — so a `CustomTabsIntent` aimed at this package fell through this
 * application's own `VIEW` filter and came back as an **ordinary tab**: the
 * address bar on top, the action row at the bottom, an entry in the tab
 * switcher. A person was handed the whole browser, with an editable address
 * bar, while a vendor's consent screen was on the glass.
 *
 * ## What it does now
 *
 * It opens the address as an errand (screen SCR-110): the same browser process,
 * so the throttle still sees the redirect, and the same window, so nothing
 * about the pinning argument above is given up — but drawn under a toolbar
 * carrying the way back, the origin read-only, and a way out, and nothing else.
 *
 * The answer stays honest in the direction that matters. `false` means no
 * surface was opened, and the provider sign-in broker reads exactly that to
 * fall back to its device-code shape rather than waiting out a deadline for a
 * page nobody was shown.
 */
@TaffyWindowScope
class AndroidAuthSurfaceAdapter @Inject constructor(
    private val errand: ErrandPagePort,
) : AuthSurfaceEndpoint {
    override suspend fun open(plan: AuthSurfacePlan): Boolean =
        withContext(Dispatchers.Main.immediate) {
            val parsed = runCatching { validate(plan.authorizationUrl) }
                .getOrElse { return@withContext false }
            errand.open(parsed.toString()) != null
        }

    private fun validate(authorizationUrl: String): URI {
        require(authorizationUrl.encodeToByteArray().size <= MAX_AUTHORIZATION_URL_BYTES) {
            "Authorization URL exceeds its bound"
        }
        val uri = URI(authorizationUrl)
        require(
            uri.scheme == HTTPS &&
                !uri.host.isNullOrBlank() &&
                uri.rawUserInfo == null &&
                uri.port == -1 &&
                uri.rawFragment == null,
        ) { "Authorization surfaces require a host-only HTTPS route" }
        return uri
    }

    private companion object {
        const val HTTPS = "https"
        const val MAX_AUTHORIZATION_URL_BYTES = 8 * 1024
    }
}
