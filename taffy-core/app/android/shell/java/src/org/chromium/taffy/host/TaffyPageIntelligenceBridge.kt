// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.pageinspector.ChromiumPageIntelligenceClient
import kotlinx.coroutines.CoroutineScope
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.content_public.browser.WebContents

/** Product-only construction seam from the profile graph to the browser page adapter. */
internal object TaffyPageIntelligenceBridge {
    fun create(
        profile: Profile,
        tab: Tab,
        webContents: WebContents,
        scope: CoroutineScope,
    ): TaffyOwnedPageIntelligenceClient {
        val owner = ChromiumPageIntelligenceClient.create(profile, tab, webContents, scope)
        return TaffyOwnedPageIntelligenceClient(owner, owner)
    }
}
