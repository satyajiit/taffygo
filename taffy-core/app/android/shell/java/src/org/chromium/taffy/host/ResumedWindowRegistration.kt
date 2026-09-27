// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.Activity
import android.app.Application
import android.os.Bundle
import com.taffygo.browser.ui.app.TaskContinuationController
import java.io.Closeable
import org.chromium.base.ActivityState
import org.chromium.base.ApplicationStatus
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge

/** Owns one registration whose authority follows an exact Activity's resumed lifetime. */
internal class ResumedWindowRegistration private constructor(
    private val owner: Activity,
    private val registration: Closeable,
    private val activate: () -> Unit,
    private val deactivate: () -> Unit,
) : Application.ActivityLifecycleCallbacks, Closeable {
    private var closed = false

    init {
        var callbackRegistered = false
        try {
            owner.application.registerActivityLifecycleCallbacks(this)
            callbackRegistered = true
            if (ApplicationStatus.getStateForActivity(owner) == ActivityState.RESUMED) activate()
        } catch (failure: Throwable) {
            closeAfterRegistrationFailure(failure, callbackRegistered)
        }
    }

    override fun onActivityResumed(activity: Activity) {
        if (!closed && activity === owner) activate()
    }

    override fun onActivityPaused(activity: Activity) {
        if (!closed && activity === owner) deactivate()
    }

    override fun onActivityDestroyed(activity: Activity) {
        if (activity === owner) close()
    }

    override fun close() {
        if (closed) return
        closed = true
        runAllTeardownOperations(
            listOf(
                { owner.application.unregisterActivityLifecycleCallbacks(this) },
                registration::close,
            ),
        )
    }

    override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) = Unit
    override fun onActivityStarted(activity: Activity) = Unit
    override fun onActivityStopped(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit

    companion object {
        fun platformSurface(
            owner: Activity,
            registration: ProfilePlatformSurfaceRegistration,
        ): ResumedWindowRegistration = ResumedWindowRegistration(
            owner,
            registration,
            activate = { registration.activate() },
            deactivate = { registration.deactivate() },
        )

        fun backup(
            owner: Activity,
            host: ChromiumBackupWindowHost,
        ): ResumedWindowRegistration = ResumedWindowRegistration(
            owner,
            host,
            activate = { host.activate() },
            deactivate = host::deactivate,
        )

        fun taskSource(
            owner: Activity,
            registration: TaffyTaskSourceSelectionBridge,
        ): ResumedWindowRegistration = ResumedWindowRegistration(
            owner,
            registration,
            activate = {
                check(registration.activate()) { "The task-source window could not be activated" }
            },
            deactivate = { registration.deactivate() },
        )

        fun permission(
            owner: Activity,
            registration: CoreApiPermissionRegistration,
        ): ResumedWindowRegistration = ResumedWindowRegistration(
            owner,
            registration,
            activate = { registration.activate() },
            deactivate = { registration.deactivate() },
        )

        fun taskContinuation(
            owner: Activity,
            controller: TaskContinuationController,
            windowToken: String,
        ): ResumedWindowRegistration = ResumedWindowRegistration(
            owner,
            Closeable { controller.setWindowVisible(windowToken, false) },
            activate = { controller.setWindowVisible(windowToken, true) },
            deactivate = { controller.setWindowVisible(windowToken, false) },
        )
    }

    private fun closeAfterRegistrationFailure(
        failure: Throwable,
        callbackRegistered: Boolean,
    ): Nothing {
        val operations = mutableListOf<() -> Unit>()
        if (callbackRegistered) {
            operations.add { owner.application.unregisterActivityLifecycleCallbacks(this) }
        }
        operations.add(registration::close)
        for (operation in operations) {
            try {
                operation()
            } catch (closeFailure: Throwable) {
                failure.addSuppressed(closeFailure)
            }
        }
        throw failure
    }
}
