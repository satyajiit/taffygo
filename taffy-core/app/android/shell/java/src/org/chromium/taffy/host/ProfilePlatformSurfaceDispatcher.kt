// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Looper
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.app.AndroidAuthSurfaceAdapter
import com.taffygo.browser.ui.app.AndroidCredentialAdapter
import com.taffygo.browser.ui.app.AuthSurfacePlan
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Refuses platform presentation unless exactly one profile window is resumed. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class ProfilePlatformSurfaceDispatcher {
    private var nextId = 1L
    private val windows = linkedMapOf<Long, WindowSurfaces>()

    fun register(
        oauth: AndroidAuthSurfaceAdapter,
        credential: AndroidCredentialAdapter,
    ): ProfilePlatformSurfaceRegistration = register(
        oauth = oauth::open,
        credential = credential::requestGoogleCredential,
    )

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    fun register(
        oauth: suspend (AuthSurfacePlan) -> Boolean,
        credential: suspend (String, String, String) -> Unit,
    ): ProfilePlatformSurfaceRegistration {
        checkMainThread()
        val id = nextId++
        windows[id] = WindowSurfaces(oauth, credential)
        return Registration(id)
    }

    suspend fun openOAuth(plan: AuthSurfacePlan): Boolean = withContext(Dispatchers.Main.immediate) {
        val target = selected() ?: return@withContext false
        runCatching { target.oauth(plan) }.getOrDefault(false)
    }

    suspend fun openGoogleCredential(
        flowId: String,
        serverClientId: String,
        hashedNonce: String,
    ): Boolean = withContext(Dispatchers.Main.immediate) {
        val target = selected() ?: return@withContext false
        try {
            target.credential(flowId, serverClientId, hashedNonce)
            true
        } catch (_: RuntimeException) {
            false
        }
    }

    fun clear() {
        checkMainThread()
        windows.clear()
    }

    private fun selected(): WindowSurfaces? {
        checkMainThread()
        var selected: WindowSurfaces? = null
        windows.values.forEach { window ->
            if (!window.active) return@forEach
            if (selected != null) return null
            selected = window
        }
        return selected
    }

    private fun activate(id: Long, active: Boolean) {
        checkMainThread()
        val window = windows[id] ?: return
        window.active = active
    }

    private inner class Registration(
        private val id: Long,
    ) : ProfilePlatformSurfaceRegistration {
        private var closed = false

        override fun activate() {
            if (!closed) activate(id, true)
        }

        override fun deactivate() {
            if (!closed) activate(id, false)
        }

        override fun close() {
            if (closed) return
            checkMainThread()
            closed = true
            windows.remove(id)
        }
    }

    private class WindowSurfaces(
        val oauth: suspend (AuthSurfacePlan) -> Boolean,
        val credential: suspend (String, String, String) -> Unit,
        var active: Boolean = false,
    )
}

private fun checkMainThread() {
    check(Looper.myLooper() == Looper.getMainLooper())
}
