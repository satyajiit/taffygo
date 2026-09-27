// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.content.Context
import com.taffygo.browser.ui.app.DaggerTaffyProcessComponent
import com.taffygo.browser.ui.app.TaffyProcessComponent
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.profiles.ProfileKeyedMap

/** Browser-process Dagger root and Chromium-owned profile-lifetime runtime map. */
class ChromiumTaffyProfileRuntimeProvider private constructor(
    applicationContext: Context,
) : TaffyProfileRuntimeProvider {
    private val processComponent: TaffyProcessComponent =
        DaggerTaffyProcessComponent.factory().create(applicationContext)
    internal val pageFactory = TaffyPageIntelligenceFactory(
        TaffyPageIntelligenceBridge::create,
    )
    private val runtimes = ProfileKeyedMap<TaffyTabProfileRuntime> { runtime ->
        runtime.close()
    }

    override fun requireRegularRuntime(profile: Profile): TaffyProfileRuntime {
        check(!profile.isOffTheRecord) { "A private profile cannot own a Window graph" }
        val runtime = runtime(profile)
        check(runtime is ChromiumTaffyProfileRuntime) {
            "A regular profile resolved to a private profile graph"
        }
        return runtime
    }

    internal fun runtime(profile: Profile): TaffyTabProfileRuntime {
        ThreadUtils.assertOnUiThread()
        check(!profile.shutdownStarted()) { "Cannot open a Taffy graph for a shutting-down profile" }
        return runtimes.getForProfile(profile) { ownedProfile ->
            if (ownedProfile.isOffTheRecord) {
                ChromiumTaffyPrivateProfileRuntime(
                    ownedProfile,
                    processComponent,
                    pageFactory,
                )
            } else {
                ChromiumTaffyProfileRuntime(ownedProfile, processComponent, this)
            }
        }
    }

    companion object {
        @Volatile
        private var instance: ChromiumTaffyProfileRuntimeProvider? = null

        /** Starts the one process root from Chromium's once-only post-native process hook. */
        @JvmStatic
        fun startFromBrowserProcess(context: Context) {
            ThreadUtils.assertOnUiThread()
            instance ?: synchronized(this) {
                instance ?: ChromiumTaffyProfileRuntimeProvider(context.applicationContext)
                    .also { instance = it }
            }
        }

        /** Returns the browser-process-owned root; Activities are consumers, never creators. */
        @JvmStatic
        fun getStarted(): TaffyProfileRuntimeProvider {
            ThreadUtils.assertOnUiThread()
            return checkNotNull(instance) {
                "Taffy process composition was not initialized by the browser process"
            }
        }
    }
}
