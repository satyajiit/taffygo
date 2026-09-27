// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.os.Handler
import android.os.Looper
import com.taffygo.browser.ui.core.api.CoreApiEndpointFailure
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import java.util.concurrent.atomic.AtomicBoolean
import org.chromium.mojo.bindings.Router
import org.chromium.taffy.core_api.mojom.CoreApiSubmissionStatus
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission

/** Owns the profile Core API pipe's failure, permission, and teardown protocol. */
internal class CoreApiEndpointTransport(
    private val proxy: TaffyProfileCoreApi,
    private val mainHandler: Handler,
    private val snapshots: CoreApiSnapshotLedger,
) : AutoCloseable {
    val closed = AtomicBoolean(false)
    val submissions = CoreApiSubmissionDispatcher(
        mainHandler = mainHandler,
        isClosed = closed::get,
        onTransportFailure = ::failTransport,
    )
    private val permissions = CoreApiPermissionDispatcher(
        invalidPermission = {
            snapshots.fail(CoreApiEndpointFailure.EnvelopeMismatch, snapshots.generation)
        },
        deliverUnavailable = ::deliverUnavailablePermission,
    )
    private var observerRouter: Router? = null

    fun attachObserver(router: Router) {
        check(Looper.myLooper() == Looper.getMainLooper())
        check(!closed.get() && observerRouter == null)
        observerRouter = router
    }

    fun acceptSnapshot(
        availabilityWire: Int,
        generationWire: Long,
        sequenceWire: Long,
        schemaVersionWire: Int,
        payload: ByteArray?,
    ) {
        if (closed.get()) return
        snapshots.accept(
            availabilityWire,
            generationWire,
            sequenceWire,
            schemaVersionWire,
            payload,
        )
    }

    fun acceptPermissionRequest(requestId: String, permissionWire: Int) {
        if (!closed.get()) permissions.accept(requestId, permissionWire)
    }

    fun registerPermissionHandler(
        handler: (String, PlatformPermission) -> Unit,
    ): CoreApiPermissionRegistration {
        check(Looper.myLooper() == Looper.getMainLooper())
        check(!closed.get())
        return permissions.register(handler)
    }

    fun settlePermission(requestId: String, permission: PlatformPermission): Boolean =
        permissions.settle(requestId, permission)

    fun failTransport() {
        submissions.failAll(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        snapshots.fail(CoreApiEndpointFailure.Disconnected, snapshots.generation)
    }

    private fun deliverUnavailablePermission(requestId: String, permission: PlatformPermission) {
        try {
            proxy.deliverPermissionResult(
                requestId,
                permission.wire.toInt(),
                PermissionDecision.UNAVAILABLE.wire.toInt(),
            ) { submissionStatus ->
                if (submissionStatus != CoreApiSubmissionStatus.ACCEPTED) failTransport()
            }
        } catch (_: RuntimeException) {
            failTransport()
        }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        snapshots.close()
        submissions.failAll(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
        val closeTransport = Runnable {
            val closingObserver = observerRouter
            observerRouter = null
            runAllTeardownOperations(
                listOf(
                    permissions::clear,
                    {
                        closingObserver?.close()
                        Unit
                    },
                    proxy::close,
                ),
            )
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            closeTransport.run()
        } else {
            mainHandler.post(closeTransport)
        }
    }
}
