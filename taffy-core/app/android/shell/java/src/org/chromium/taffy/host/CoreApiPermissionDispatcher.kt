// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Looper
import androidx.annotation.VisibleForTesting
import taffy.core_api.PlatformPermission

/** Refuses a permission request unless exactly one Android window is resumed. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
class CoreApiPermissionDispatcher(
    private val invalidPermission: () -> Unit,
    private val deliverUnavailable: (String, PlatformPermission) -> Unit,
) {
    private var nextHandlerId = 1L
    private val handlers = linkedMapOf<Long, PermissionHandler>()
    private val pending = linkedMapOf<String, PendingPermission>()

    fun register(handler: (String, PlatformPermission) -> Unit): CoreApiPermissionRegistration {
        checkMainThread()
        val id = nextHandlerId++
        handlers[id] = PermissionHandler(id, handler)
        return Registration(id)
    }

    fun accept(requestId: String, permissionWire: Int) {
        checkMainThread()
        val permission = PlatformPermission.fromWire(permissionWire.toUInt())
        if (permission == null) {
            invalidPermission()
            return
        }
        if (pending.containsKey(requestId)) {
            invalidPermission()
            return
        }
        var target: PermissionHandler? = null
        for (candidate in handlers.values) {
            if (!candidate.active) continue
            if (target != null) {
                target = null
                break
            }
            target = candidate
        }
        if (target == null) {
            deliverUnavailable(requestId, permission)
            return
        }
        pending[requestId] = PendingPermission(target.id, permission)
        try {
            target.handler(requestId, permission)
        } catch (_: RuntimeException) {
            pending.remove(requestId)
            deliverUnavailable(requestId, permission)
        }
    }

    fun clear() {
        checkMainThread()
        handlers.clear()
        pending.clear()
    }

    /** Claims the one browser request assigned to a Window before forwarding its result. */
    fun settle(requestId: String, permission: PlatformPermission): Boolean {
        checkMainThread()
        val assignment = pending[requestId] ?: return false
        if (assignment.permission != permission) {
            invalidPermission()
            return false
        }
        pending.remove(requestId)
        return true
    }

    private fun activate(id: Long, active: Boolean) {
        checkMainThread()
        val entry = handlers[id] ?: return
        entry.active = active
    }

    private inner class Registration(
        private val id: Long,
    ) : CoreApiPermissionRegistration {
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
            handlers.remove(id)
            val abandoned = pending.entries
                .filter { it.value.handlerId == id }
                .map { it.key to it.value.permission }
            abandoned.forEach { (requestId, _) -> pending.remove(requestId) }
            abandoned.forEach { (requestId, permission) ->
                deliverUnavailable(requestId, permission)
            }
        }
    }

    private class PermissionHandler(
        val id: Long,
        val handler: (String, PlatformPermission) -> Unit,
        var active: Boolean = false,
    )

    private data class PendingPermission(
        val handlerId: Long,
        val permission: PlatformPermission,
    )
}

private fun checkMainThread() {
    check(Looper.myLooper() == Looper.getMainLooper())
}
