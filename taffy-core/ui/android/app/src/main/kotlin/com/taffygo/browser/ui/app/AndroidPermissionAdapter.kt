// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.provider.Settings
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.net.toUri
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.PlatformPermissionRequester
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowScope
import java.io.Closeable
import javax.inject.Inject
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import taffy.core_api.PermissionDecision
import taffy.core_api.PlatformPermission
import kotlin.coroutines.resume

/** Android runtime-permission surface for portable Core API permission names. */
@TaffyWindowScope
class AndroidPermissionAdapter @Inject constructor(
    private val activity: Activity,
    private val core: CoreApiClient,
    private val lifetime: TaffyWindowLifetime,
) : PermissionEndpoint, PlatformPermissionRequester, Closeable {
    private val pending = AndroidPermissionRequestLedger()
    private val direct = DirectPermissionRequestLedger()
    private val promptHistory = activity.getSharedPreferences(
        PERMISSION_HISTORY_STORE,
        Context.MODE_PRIVATE,
    )

    override fun request(requestId: String, permission: PlatformPermission) {
        val androidPermission = permission.androidPermission()
        if (androidPermission == null) {
            deliver(requestId, permission, permission.immediateSnapshot().decision)
            return
        }
        if (ContextCompat.checkSelfPermission(activity, androidPermission) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            deliver(requestId, permission, PermissionDecision.GRANTED)
            return
        }
        when (val admission = pending.admit(requestId, permission)) {
            is PendingPermissionAdmission.Accepted -> {
                try {
                    ActivityCompat.requestPermissions(
                        activity,
                        arrayOf(androidPermission),
                        admission.requestCode,
                    )
                } catch (_: RuntimeException) {
                    pending.rollback(admission.requestCode)
                    deliver(requestId, permission, PermissionDecision.UNAVAILABLE)
                }
            }
            PendingPermissionAdmission.Duplicate -> Unit
            PendingPermissionAdmission.Full,
            PendingPermissionAdmission.Closed,
            -> deliver(requestId, permission, PermissionDecision.UNAVAILABLE)
        }
    }

    override fun current(
        permission: PlatformPermission,
    ): PlatformPermissionRequester.Snapshot = try {
        val androidPermission = permission.androidPermission()
            ?: return permission.immediateSnapshot()
        if (ContextCompat.checkSelfPermission(activity, androidPermission) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            grantedPermissionSnapshot()
        } else {
            deniedPermissionSnapshot(permission, androidPermission)
        }
    } catch (_: RuntimeException) {
        unavailablePermissionSnapshot()
    }

    override suspend fun request(
        permission: PlatformPermission,
    ): PlatformPermissionRequester.Snapshot {
        val before = current(permission)
        if (before.decision == PermissionDecision.GRANTED || !before.canRequest) return before
        val androidPermission = permission.androidPermission()
            ?: return permission.immediateSnapshot()
        return suspendCancellableCoroutine { continuation ->
            when (
                val admission = direct.admit(permission) { snapshot ->
                    if (continuation.isActive) continuation.resume(snapshot)
                }
            ) {
                is DirectPermissionAdmission.Accepted -> {
                    continuation.invokeOnCancellation { direct.detach(admission.identity) }
                    try {
                        ActivityCompat.requestPermissions(
                            activity,
                            arrayOf(androidPermission),
                            DIRECT_PERMISSION_REQUEST_CODE,
                        )
                    } catch (_: RuntimeException) {
                        direct.fail(admission.identity)?.deliver(unavailablePermissionSnapshot())
                    }
                }
                DirectPermissionAdmission.Busy,
                DirectPermissionAdmission.Closed,
                -> continuation.resume(unavailablePermissionSnapshot())
            }
        }
    }

    override fun openSettings(permission: PlatformPermission): Boolean {
        val intent = if (permission == PlatformPermission.NOTIFICATIONS) {
            Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).apply {
                putExtra(Settings.EXTRA_APP_PACKAGE, activity.packageName)
            }
        } else {
            Intent(
                Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                "package:${activity.packageName}".toUri(),
            )
        }
        return try {
            activity.startActivity(intent)
            true
        } catch (_: RuntimeException) {
            false
        }
    }

    fun onRequestPermissionsResult(requestCode: Int, grantResults: IntArray): Boolean {
        if (requestCode == DIRECT_PERMISSION_REQUEST_CODE) {
            val request = direct.settle() ?: return true
            val snapshot = when (grantResults.firstOrNull()) {
                PackageManager.PERMISSION_GRANTED -> grantedPermissionSnapshot()
                null -> unavailablePermissionSnapshot()
                else -> {
                    rememberPrompt(request.permission)
                    request.permission.androidPermission()
                        ?.let { permission ->
                            try {
                                deniedPermissionSnapshot(request.permission, permission)
                            } catch (_: RuntimeException) {
                                unavailablePermissionSnapshot()
                            }
                        }
                        ?: request.permission.immediateSnapshot()
                }
            }
            request.deliver(snapshot)
            return true
        }
        val request = pending.settle(requestCode) ?: return false
        val decision = when (grantResults.firstOrNull()) {
            PackageManager.PERMISSION_GRANTED -> PermissionDecision.GRANTED
            null -> PermissionDecision.UNAVAILABLE
            else -> {
                rememberPrompt(request.permission)
                PermissionDecision.DENIED
            }
        }
        deliver(request.requestId, request.permission, decision)
        return true
    }

    private fun deliver(
        requestId: String,
        permission: PlatformPermission,
        decision: PermissionDecision,
    ) {
        lifetime.scope.launch { core.deliverPermissionResult(requestId, permission, decision) }
    }

    /**
     * Drops platform callbacks after the Window disappears. The profile endpoint owns the
     * corresponding request identities and settles them unavailable when its Window registration
     * closes; doing that above this adapter keeps teardown terminal even though this Window scope
     * is deliberately cancelled before its resources close.
     */
    override fun close() {
        pending.close()
        direct.takeAndClose()?.deliver(unavailablePermissionSnapshot())
    }

    private fun deniedPermissionSnapshot(
        permission: PlatformPermission,
        androidPermission: String,
    ): PlatformPermissionRequester.Snapshot {
        val showRationale = ActivityCompat.shouldShowRequestPermissionRationale(
            activity,
            androidPermission,
        )
        val userDecisionRecorded = promptHistory.getBoolean(permission.name, false)
        return projectDeniedPermissionSnapshot(showRationale, userDecisionRecorded)
    }

    private fun rememberPrompt(permission: PlatformPermission) {
        promptHistory.edit { putBoolean(permission.name, true) }
    }

    private companion object {
        // The Core API adapter owns 0x5440..0x547f and voice owns 0x5480.
        const val DIRECT_PERMISSION_REQUEST_CODE = 0x543f
        const val PERMISSION_HISTORY_STORE = "taffy_platform_permission_requests"
    }

}

/** A bounded, close-once bijection between browser request identities and Android request codes. */
internal class AndroidPermissionRequestLedger(
    private val maxPending: Int = MAX_PENDING_ANDROID_PERMISSIONS,
    firstRequestCode: Int = FIRST_REQUEST_CODE,
) : Closeable {
    private val lock = Any()
    private val lastRequestCode = firstRequestCode + maxPending - 1
    private val pendingByCode = linkedMapOf<Int, PendingAndroidPermission>()
    private val codeByRequestId = linkedMapOf<String, Int>()
    private var nextRequestCode = firstRequestCode
    private var closed = false

    init {
        require(maxPending > 0)
        require(firstRequestCode >= 0)
        require(lastRequestCode <= MAX_ANDROID_REQUEST_CODE)
    }

    fun admit(
        requestId: String,
        permission: PlatformPermission,
    ): PendingPermissionAdmission = synchronized(lock) {
        if (closed) return@synchronized PendingPermissionAdmission.Closed
        if (codeByRequestId.containsKey(requestId)) {
            return@synchronized PendingPermissionAdmission.Duplicate
        }
        if (pendingByCode.size >= maxPending) {
            return@synchronized PendingPermissionAdmission.Full
        }
        repeat(maxPending) {
            val candidate = nextRequestCode
            nextRequestCode = if (candidate == lastRequestCode) {
                lastRequestCode - maxPending + 1
            } else {
                candidate + 1
            }
            if (!pendingByCode.containsKey(candidate)) {
                pendingByCode[candidate] = PendingAndroidPermission(requestId, permission)
                codeByRequestId[requestId] = candidate
                return@synchronized PendingPermissionAdmission.Accepted(candidate)
            }
        }
        error("A non-full permission ledger had no free request code")
    }

    fun settle(requestCode: Int): PendingAndroidPermission? = synchronized(lock) {
        val request = pendingByCode.remove(requestCode) ?: return@synchronized null
        check(codeByRequestId.remove(request.requestId) == requestCode)
        request
    }

    fun rollback(requestCode: Int) {
        settle(requestCode)
    }

    override fun close() {
        synchronized(lock) {
            if (closed) return
            closed = true
            pendingByCode.clear()
            codeByRequestId.clear()
        }
    }

    internal companion object {
        const val FIRST_REQUEST_CODE = 0x5440
        // The portable contract admits at most 64 pending permissions per profile. A Window may
        // own all of them, so this platform ledger must neither truncate that bound nor grow
        // past it.
        private const val MAX_PENDING_ANDROID_PERMISSIONS = 64
        private const val MAX_ANDROID_REQUEST_CODE = 0xffff
    }
}

internal sealed interface PendingPermissionAdmission {
    data class Accepted(val requestCode: Int) : PendingPermissionAdmission
    data object Duplicate : PendingPermissionAdmission
    data object Full : PendingPermissionAdmission
    data object Closed : PendingPermissionAdmission
}

internal data class PendingAndroidPermission(
    val requestId: String,
    val permission: PlatformPermission,
)

private fun PlatformPermission.androidPermission(): String? = when (this) {
    PlatformPermission.NOTIFICATIONS ->
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.POST_NOTIFICATIONS
        } else {
            null
        }
    PlatformPermission.CAMERA -> Manifest.permission.CAMERA
    PlatformPermission.MICROPHONE -> Manifest.permission.RECORD_AUDIO
    PlatformPermission.LOCATION -> Manifest.permission.ACCESS_FINE_LOCATION
    PlatformPermission.READ_USER_FILE,
    PlatformPermission.WRITE_USER_FILE,
    -> null
}

private fun PlatformPermission.immediateSnapshot(): PlatformPermissionRequester.Snapshot =
    when (this) {
        PlatformPermission.NOTIFICATIONS -> grantedPermissionSnapshot()
        PlatformPermission.READ_USER_FILE,
        PlatformPermission.WRITE_USER_FILE,
        -> unavailablePermissionSnapshot()
        PlatformPermission.CAMERA,
        PlatformPermission.MICROPHONE,
        PlatformPermission.LOCATION,
        -> unavailablePermissionSnapshot()
    }

private fun grantedPermissionSnapshot() = PlatformPermissionRequester.Snapshot(
    decision = PermissionDecision.GRANTED,
    canRequest = false,
    shouldShowRationale = false,
)

private fun unavailablePermissionSnapshot() = PlatformPermissionRequester.Snapshot(
    decision = PermissionDecision.UNAVAILABLE,
    canRequest = false,
    shouldShowRationale = false,
)

/** Projects Android's ambiguous denied state without inventing another permission enumeration. */
internal fun projectDeniedPermissionSnapshot(
    showRationale: Boolean,
    userDecisionRecorded: Boolean,
) = PlatformPermissionRequester.Snapshot(
    decision = PermissionDecision.DENIED,
    canRequest = showRationale || !userDecisionRecorded,
    shouldShowRationale = showRationale,
)
