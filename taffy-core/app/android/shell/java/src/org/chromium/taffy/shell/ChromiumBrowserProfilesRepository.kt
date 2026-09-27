// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.Intent
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import java.io.Closeable
import kotlin.coroutines.resume
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.profiles.Profile as ChromiumProfile
import org.chromium.taffy.browser.TaffyBrowserProfilesBridge

/** Chromium-backed regular-profile store for one trusted product window. */
class ChromiumBrowserProfilesRepository @VisibleForTesting constructor(
    private val activity: Activity,
    private val currentProfile: ChromiumProfile,
    private val bridge: LifecycleBridge = NativeBrowserProfileLifecycleBridge,
) : BrowserProfilesRepository {
    private val mutableSnapshot = MutableStateFlow(BrowserProfilesRepository.Snapshot())
    private var restartStarted = false

    override val snapshot: StateFlow<BrowserProfilesRepository.Snapshot> = mutableSnapshot

    override fun refresh() {
        if (currentProfile.isOffTheRecord || activity.isDestroyed) {
            unavailable()
            return
        }
        val profiles = try {
            bridge.listProfiles(currentProfile)
        } catch (_: RuntimeException) {
            unavailable()
            return
        }
        mutableSnapshot.value = if (
            profiles.isNotEmpty() &&
            profiles.count(BrowserProfilesRepository.Profile::active) == 1
        ) {
            BrowserProfilesRepository.Snapshot(
                availability = BrowserProfilesRepository.Availability.READY,
                profiles = profiles.map(::withProductName),
            )
        } else {
            BrowserProfilesRepository.Snapshot(
                availability = BrowserProfilesRepository.Availability.UNAVAILABLE,
            )
        }
    }

    override suspend fun create(displayName: String): BrowserProfilesRepository.Result {
        val native = await { callback ->
            bridge.createProfile(currentProfile, displayName, callback)
        }
        val profile = native.profile
        val failure = native.failure ?: if (profile == null) {
            BrowserProfilesRepository.Failure.FAILED
        } else {
            null
        }
        if (failure == null && profile != null) relaunch(profile)
        return BrowserProfilesRepository.Result(failure)
    }

    override suspend fun activate(profileId: String): BrowserProfilesRepository.Result {
        val native = await { callback ->
            bridge.activateProfile(currentProfile, profileId, callback)
        }
        val profile = native.profile
        val failure = native.failure ?: if (profile == null) {
            BrowserProfilesRepository.Failure.FAILED
        } else {
            null
        }
        if (failure == null && profile != null) relaunch(profile)
        return BrowserProfilesRepository.Result(failure)
    }

    override suspend fun delete(profileId: String): BrowserProfilesRepository.Result {
        val native = await { callback ->
            bridge.deleteProfile(currentProfile, profileId, callback)
        }
        val result = BrowserProfilesRepository.Result(native.failure)
        if (result.succeeded) refresh()
        return result
    }

    private suspend fun await(
        start: ((LifecycleResult) -> Unit) -> Unit,
    ): LifecycleResult = suspendCancellableCoroutine { continuation ->
        start { result ->
            if (continuation.isActive) continuation.resume(result)
        }
    }

    private fun relaunch(resolvedProfile: ChromiumProfile) {
        if (resolvedProfile.isOffTheRecord) return
        if (restartStarted || activity.isFinishing || activity.isDestroyed) return
        restartStarted = true
        activity.startActivity(Intent.makeRestartActivityTask(activity.componentName))
        activity.finish()
    }

    /**
     * A name Chromium chose is replaced with the product's own before any screen
     * reads it: the first profile would otherwise say "Your Chromium" on SCR-708 and
     * on the workspace list (decision 0255). A name a person chose is kept as it is.
     */
    private fun withProductName(
        profile: BrowserProfilesRepository.Profile,
    ): BrowserProfilesRepository.Profile = if (profile.usesDefaultName) {
        profile.copy(displayName = activity.getString(R.string.taffy_profile_default_name))
    } else {
        profile
    }

    private fun unavailable() {
        mutableSnapshot.value = BrowserProfilesRepository.Snapshot(
            availability = BrowserProfilesRepository.Availability.UNAVAILABLE,
        )
    }

    @VisibleForTesting
    data class LifecycleResult(
        val profile: ChromiumProfile? = null,
        val failure: BrowserProfilesRepository.Failure? = null,
    )

    @VisibleForTesting
    interface LifecycleBridge {
        fun listProfiles(currentProfile: ChromiumProfile): List<BrowserProfilesRepository.Profile>

        fun createProfile(
            currentProfile: ChromiumProfile,
            displayName: String,
            callback: (LifecycleResult) -> Unit,
        )

        fun activateProfile(
            currentProfile: ChromiumProfile,
            profileId: String,
            callback: (LifecycleResult) -> Unit,
        )

        fun deleteProfile(
            currentProfile: ChromiumProfile,
            profileId: String,
            callback: (LifecycleResult) -> Unit,
        )
    }
}

/** Acquires one UI-thread lease for the exact browser window and releases it once. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun openBrowserProfileWindowLease(
    windowProfile: ChromiumProfile,
    registerLease: (ChromiumProfile) -> Long = TaffyBrowserProfilesBridge::registerWindowLease,
    unregisterLease: (Long) -> Unit = TaffyBrowserProfilesBridge::unregisterWindowLease,
): Closeable {
    ThreadUtils.assertOnUiThread()
    val leaseId = registerLease(windowProfile)
    check(leaseId > 0) { "The browser window profile could not be leased" }
    var closed = false
    return Closeable {
        ThreadUtils.assertOnUiThread()
        if (!closed) {
            unregisterLease(leaseId)
            closed = true
        }
    }
}

private object NativeBrowserProfileLifecycleBridge : ChromiumBrowserProfilesRepository.LifecycleBridge {
    override fun listProfiles(
        currentProfile: ChromiumProfile,
    ): List<BrowserProfilesRepository.Profile> =
        TaffyBrowserProfilesBridge.listProfiles(currentProfile).map { profile ->
            BrowserProfilesRepository.Profile(
                id = profile.id(),
                displayName = profile.displayName(),
                active = profile.active(),
                usesDefaultName = profile.usesDefaultName(),
            )
        }

    override fun createProfile(
        currentProfile: ChromiumProfile,
        displayName: String,
        callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
    ) {
        TaffyBrowserProfilesBridge.createProfile(currentProfile, displayName) { result ->
            callback(result.toLifecycleResult())
        }
    }

    override fun activateProfile(
        currentProfile: ChromiumProfile,
        profileId: String,
        callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
    ) {
        TaffyBrowserProfilesBridge.activateProfile(currentProfile, profileId) { result ->
            callback(result.toLifecycleResult())
        }
    }

    override fun deleteProfile(
        currentProfile: ChromiumProfile,
        profileId: String,
        callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
    ) {
        TaffyBrowserProfilesBridge.deleteProfile(currentProfile, profileId) { result ->
            callback(result.toLifecycleResult())
        }
    }
}

private fun TaffyBrowserProfilesBridge.OperationResult.toLifecycleResult() =
    ChromiumBrowserProfilesRepository.LifecycleResult(
        profile = profile(),
        failure = mapBrowserProfileLifecycleFailure(error()),
    )

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun mapBrowserProfileLifecycleFailure(error: Int): BrowserProfilesRepository.Failure? =
    when (error) {
        TaffyBrowserProfilesBridge.ERROR_NONE -> null
        TaffyBrowserProfilesBridge.ERROR_UNAVAILABLE -> BrowserProfilesRepository.Failure.UNAVAILABLE
        TaffyBrowserProfilesBridge.ERROR_PRIVATE_PROFILE ->
            BrowserProfilesRepository.Failure.PRIVATE_PROFILE
        TaffyBrowserProfilesBridge.ERROR_NOT_ACTIVE -> BrowserProfilesRepository.Failure.NOT_ACTIVE
        TaffyBrowserProfilesBridge.ERROR_INVALID_NAME -> BrowserProfilesRepository.Failure.INVALID_NAME
        TaffyBrowserProfilesBridge.ERROR_LIMIT_REACHED -> BrowserProfilesRepository.Failure.LIMIT_REACHED
        TaffyBrowserProfilesBridge.ERROR_DUPLICATE_NAME ->
            BrowserProfilesRepository.Failure.DUPLICATE_NAME
        TaffyBrowserProfilesBridge.ERROR_NOT_FOUND -> BrowserProfilesRepository.Failure.NOT_FOUND
        TaffyBrowserProfilesBridge.ERROR_ACTIVE_PROFILE -> BrowserProfilesRepository.Failure.ACTIVE_PROFILE
        TaffyBrowserProfilesBridge.ERROR_LAST_PROFILE -> BrowserProfilesRepository.Failure.LAST_PROFILE
        TaffyBrowserProfilesBridge.ERROR_BUSY -> BrowserProfilesRepository.Failure.BUSY
        TaffyBrowserProfilesBridge.ERROR_PROFILE_IN_USE ->
            BrowserProfilesRepository.Failure.PROFILE_IN_USE
        else -> BrowserProfilesRepository.Failure.FAILED
    }
