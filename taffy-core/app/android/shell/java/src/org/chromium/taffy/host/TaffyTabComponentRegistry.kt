// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.TaffyTabComponentParent
import java.io.Closeable
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab

/** Profile-owned Tab component map; selector/window attachment never owns an entry. */
internal class TaffyTabComponentRegistry(
    private val profile: Profile,
    private val profileComponent: TaffyTabComponentParent,
    private val pageFactory: TaffyPageIntelligenceFactory,
) : Closeable {
    private val owners = TaffyOwnedObjectMap<Tab, TaffyTabComponentOwner>()
    private var closed = false

    fun require(tab: Tab): TaffyTabComponentOwner {
        check(!closed) { "Cannot attach a Tab to a closed profile registry" }
        check(tab.profile === profile) { "A Tab registry cannot cross Chromium profiles" }
        return owners.getOrCreate(tab) {
            TaffyTabComponentOwner(
                profile,
                tab,
                profileComponent,
                pageFactory,
            ) { destroyed -> owners.forget(tab, destroyed) }
        }
    }

    override fun close() {
        if (closed) return
        closed = true
        owners.close()
    }
}
