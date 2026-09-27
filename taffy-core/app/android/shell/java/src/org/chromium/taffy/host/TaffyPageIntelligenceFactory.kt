// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import kotlinx.coroutines.CoroutineScope
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.content_public.browser.WebContents

/** Constructs the production browser adapter after the Tab lifetime scope exists. */
internal fun interface TaffyPageIntelligenceFactory {
    fun create(
        profile: Profile,
        tab: Tab,
        webContents: WebContents,
        scope: CoroutineScope,
    ): TaffyOwnedPageIntelligenceClient
}
