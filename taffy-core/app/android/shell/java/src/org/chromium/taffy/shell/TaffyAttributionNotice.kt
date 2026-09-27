// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import org.chromium.chrome.browser.tab.TabLaunchType
import org.chromium.chrome.browser.tabmodel.TabCreatorManager
import org.chromium.taffy.browser.TaffyAttributionNoticeBridge

/**
 * The one place the app opens one of the engine's own pages: the third-party notices About
 * shows (decision 0206).
 *
 * Decision 0154 still holds everywhere else. [ChromiumBrowserMediator.navigateTo] and the
 * address a new tab opens on both refuse an engine page, because a string reaching them may have
 * come from a person or a page. Nothing reaches this function as a string. The address comes from
 * the browser process's product identity through [TaffyAttributionNoticeBridge], so the next
 * caller cannot use it to open any other page.
 *
 * It opens as a regular tab: the notice is the same for everyone and holds nothing to keep private.
 */
internal object TaffyAttributionNotice {

    /** Opens the notice in a new selected tab. Main thread only; false when nothing opened. */
    fun open(tabCreators: TabCreatorManager): Boolean {
        val address = TaffyAttributionNoticeBridge.attributionResourcePath()
        if (address.isEmpty()) return false
        return tabCreators
            .getTabCreator(/* incognito= */ false)
            .launchUrl(address, TabLaunchType.FROM_CHROME_UI) != null
    }
}
