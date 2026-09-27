// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.TaffyPrivateProfileComponent
import com.taffygo.browser.ui.app.TaffyProcessComponent
import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab

/** Ephemeral private profile: profile lifetime plus movable Tab graphs, with no Window graph. */
internal class ChromiumTaffyPrivateProfileRuntime(
    private val profile: Profile,
    processComponent: TaffyProcessComponent,
    pageFactory: TaffyPageIntelligenceFactory,
) : TaffyTabProfileRuntime {
    private val component: TaffyPrivateProfileComponent = processComponent.privateProfileBuilder()
        .identity(TaffyProfileIdentity(opaqueProfileToken(profile, expectPrivate = true), true))
        .build()
    private val tabs = TaffyTabComponentRegistry(profile, component, pageFactory)
    private var closed = false

    override fun requireTab(tab: Tab): TaffyTabComponentOwner {
        check(!closed) { "Cannot open a tab in a closed private profile graph" }
        check(tab.profile === profile) { "A private Tab graph cannot cross its Chromium profile" }
        return tabs.require(tab)
    }

    override fun close() {
        if (closed) return
        closed = true
        closeAllOwnedResources(listOf(tabs, component.lifetime()))
    }
}
