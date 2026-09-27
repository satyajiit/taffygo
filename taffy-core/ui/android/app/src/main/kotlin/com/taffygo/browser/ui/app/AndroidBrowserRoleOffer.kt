// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.app.Activity
import android.app.role.RoleManager
import android.content.Context
import androidx.core.content.edit
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import com.taffygo.browser.ui.core.ui.BrowserRoleOffer
import com.taffygo.browser.ui.core.ui.BrowserRoleOfferResult
import javax.inject.Inject

/** Android-owned implementation of the one post-value default-browser offer. */
@TaffyWindowScope
class AndroidBrowserRoleOffer @Inject constructor(
    private val activity: Activity,
) : BrowserRoleOffer {
    private val preferences = activity.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE,
    )

    override fun offerAfterAcceptedOutput(): BrowserRoleOfferResult = synchronized(preferences) {
        if (!activity.isVisibleForRoleOffer()) return@synchronized BrowserRoleOfferResult.NOT_VISIBLE

        val manager = try {
            activity.getSystemService(RoleManager::class.java)
        } catch (_: RuntimeException) {
            null
        } ?: return@synchronized BrowserRoleOfferResult.UNSUPPORTED

        val available = try {
            manager.isRoleAvailable(RoleManager.ROLE_BROWSER)
        } catch (_: RuntimeException) {
            false
        }
        if (!available) return@synchronized BrowserRoleOfferResult.UNSUPPORTED

        val held = try {
            manager.isRoleHeld(RoleManager.ROLE_BROWSER)
        } catch (_: RuntimeException) {
            false
        }
        if (held) return@synchronized BrowserRoleOfferResult.ALREADY_DEFAULT
        if (preferences.getBoolean(OFFERED_KEY, false)) {
            return@synchronized BrowserRoleOfferResult.ALREADY_OFFERED
        }

        val intent = try {
            manager.createRequestRoleIntent(RoleManager.ROLE_BROWSER)
        } catch (_: RuntimeException) {
            return@synchronized BrowserRoleOfferResult.FAILED
        }
        return@synchronized try {
            activity.startActivity(intent)
            preferences.edit { putBoolean(OFFERED_KEY, true) }
            BrowserRoleOfferResult.SHEET_LAUNCHED
        } catch (_: RuntimeException) {
            BrowserRoleOfferResult.FAILED
        }
    }

    private fun Activity.isVisibleForRoleOffer(): Boolean =
        !isFinishing &&
            !isDestroyed &&
            (this as? LifecycleOwner)?.lifecycle?.currentState?.isAtLeast(Lifecycle.State.RESUMED) ==
            true

    private companion object {
        const val PREFERENCES_NAME = "taffy.browser_role"
        const val OFFERED_KEY = "platform_sheet_offered"
    }
}
