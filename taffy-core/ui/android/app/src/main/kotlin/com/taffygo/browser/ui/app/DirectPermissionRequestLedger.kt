// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import taffy.core_api.PlatformPermission

/** One close-once direct UI permission request, separate from Core API request identities. */
internal class DirectPermissionRequestLedger {
    private val lock = Any()
    private var nextIdentity = 1L
    private var pending: PendingDirectPermission? = null
    private var closed = false

    fun admit(
        permission: PlatformPermission,
        deliver: (PlatformPermissionRequester.Snapshot) -> Unit,
    ): DirectPermissionAdmission = synchronized(lock) {
        if (closed) return@synchronized DirectPermissionAdmission.Closed
        if (pending != null) return@synchronized DirectPermissionAdmission.Busy
        val identity = nextIdentity++
        pending = PendingDirectPermission(identity, permission, deliver)
        DirectPermissionAdmission.Accepted(identity)
    }

    /** Keeps the request-code claim but drops a cancelled caller's callback. */
    fun detach(identity: Long) {
        synchronized(lock) {
            pending?.takeIf { it.identity == identity }?.completion = null
        }
    }

    fun fail(identity: Long): PendingDirectPermission? = synchronized(lock) {
        pending?.takeIf { it.identity == identity }?.also { pending = null }
    }

    fun settle(): PendingDirectPermission? = synchronized(lock) {
        pending.also { pending = null }
    }

    fun takeAndClose(): PendingDirectPermission? = synchronized(lock) {
        if (closed) return@synchronized null
        closed = true
        pending.also { pending = null }
    }
}

internal sealed interface DirectPermissionAdmission {
    data class Accepted(val identity: Long) : DirectPermissionAdmission
    data object Busy : DirectPermissionAdmission
    data object Closed : DirectPermissionAdmission
}

internal class PendingDirectPermission(
    val identity: Long,
    val permission: PlatformPermission,
    var completion: ((PlatformPermissionRequester.Snapshot) -> Unit)?,
) {
    fun deliver(snapshot: PlatformPermissionRequester.Snapshot) {
        completion?.invoke(snapshot)
    }
}
