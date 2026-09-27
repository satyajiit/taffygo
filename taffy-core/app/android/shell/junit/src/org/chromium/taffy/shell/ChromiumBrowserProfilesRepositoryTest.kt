// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.app.Activity
import android.content.Intent
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.taffy.browser.TaffyBrowserProfilesBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.`when`
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBrowserProfilesRepositoryTest {
    private val currentProfile = regularProfile()

    @Test
    fun `refresh publishes only a nonempty snapshot with one active profile`() {
        val bridge = FakeBridge(
            profiles = listOf(
                profile("personal", "Personal", active = true),
                profile("work", "Work", active = false),
            ),
        )
        val repository = repository(bridge)

        repository.refresh()
        assertEquals(
            BrowserProfilesRepository.Availability.READY,
            repository.snapshot.value.availability,
        )
        assertEquals(listOf("personal", "work"), repository.snapshot.value.profiles.map { it.id })

        bridge.profiles = listOf(
            profile("personal", "Personal", active = true),
            profile("work", "Work", active = true),
        )
        repository.refresh()
        assertEquals(
            BrowserProfilesRepository.Availability.UNAVAILABLE,
            repository.snapshot.value.availability,
        )
        assertTrue(repository.snapshot.value.profiles.isEmpty())
    }

    @Test
    fun `a name Chromium chose is shown as the product's own and a person's name is kept`() {
        val activity = activity()
        val bridge = FakeBridge(
            profiles = listOf(
                BrowserProfilesRepository.Profile(
                    id = "default",
                    displayName = "Your Chromium",
                    active = true,
                    usesDefaultName = true,
                ),
                profile("work", "Work", active = false),
            ),
        )
        val repository = repository(bridge, activity)

        repository.refresh()

        val names = repository.snapshot.value.profiles.map { it.displayName }
        assertEquals(
            listOf(activity.getString(R.string.taffy_profile_default_name), "Work"),
            names,
        )
        assertEquals("Your TaffyGo", names.first())
    }

    @Test
    fun `successful create requires a resolved regular profile before relaunch`() = runBlocking {
        val activity = activity()
        val bridge = FakeBridge(createResult = ChromiumBrowserProfilesRepository.LifecycleResult())
        val repository = repository(bridge, activity)

        val result = repository.create("Work")

        assertEquals(BrowserProfilesRepository.Failure.FAILED, result.failure)
        assertNull(shadowOf(activity).nextStartedActivity)
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `create switches with a clean task restart only after native success`() = runBlocking {
        val activity = activity()
        val createdProfile = regularProfile()
        val bridge = FakeBridge(
            createResult = ChromiumBrowserProfilesRepository.LifecycleResult(profile = createdProfile),
        )
        val repository = repository(bridge, activity)

        val result = repository.create("Work")
        val restart = shadowOf(activity).nextStartedActivity

        assertTrue(result.succeeded)
        assertEquals(activity.componentName, restart.component)
        assertTrue(restart.flags and Intent.FLAG_ACTIVITY_NEW_TASK != 0)
        assertTrue(restart.flags and Intent.FLAG_ACTIVITY_CLEAR_TASK != 0)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `delete refreshes only after confirmed native completion`() = runBlocking {
        val bridge = FakeBridge(
            profiles = listOf(profile("personal", "Personal", active = true)),
            deleteResult = ChromiumBrowserProfilesRepository.LifecycleResult(),
        )
        val repository = repository(bridge)

        val result = repository.delete("work")

        assertTrue(result.succeeded)
        assertEquals(1, bridge.listCalls)
        assertEquals(
            BrowserProfilesRepository.Availability.READY,
            repository.snapshot.value.availability,
        )
    }

    @Test
    fun `profile in use failure is preserved without refreshing`() = runBlocking {
        val bridge = FakeBridge(
            deleteResult = ChromiumBrowserProfilesRepository.LifecycleResult(
                failure = BrowserProfilesRepository.Failure.PROFILE_IN_USE,
            ),
        )
        val repository = repository(bridge)

        val result = repository.delete("work")

        assertEquals(BrowserProfilesRepository.Failure.PROFILE_IN_USE, result.failure)
        assertEquals(0, bridge.listCalls)
    }

    @Test
    fun `window lease registers the exact profile and releases only once`() {
        val released = mutableListOf<Long>()
        val lease = openBrowserProfileWindowLease(
            currentProfile,
            registerLease = { profile ->
                assertSame(currentProfile, profile)
                41L
            },
            unregisterLease = { released += it },
        )

        lease.close()
        lease.close()

        assertEquals(listOf(41L), released)
    }

    @Test
    fun `window construction fails closed when native cannot issue a lease`() {
        var refused = false
        try {
            openBrowserProfileWindowLease(
                currentProfile,
                registerLease = { 0L },
                unregisterLease = {},
            )
        } catch (_: IllegalStateException) {
            refused = true
        }
        assertTrue(refused)
    }

    @Test
    fun `native profile in use result maps without collapsing to generic failure`() {
        assertEquals(
            BrowserProfilesRepository.Failure.PROFILE_IN_USE,
            mapBrowserProfileLifecycleFailure(TaffyBrowserProfilesBridge.ERROR_PROFILE_IN_USE),
        )
    }

    private fun repository(
        bridge: FakeBridge,
        activity: Activity = activity(),
    ) = ChromiumBrowserProfilesRepository(activity, currentProfile, bridge)

    private fun activity(): Activity = Robolectric.buildActivity(Activity::class.java).setup().get()

    private fun regularProfile(): Profile = mock(Profile::class.java).also { profile ->
        `when`(profile.isOffTheRecord).thenReturn(false)
    }

    private fun profile(
        id: String,
        name: String,
        active: Boolean,
    ) = BrowserProfilesRepository.Profile(id, name, active)

    private class FakeBridge(
        var profiles: List<BrowserProfilesRepository.Profile> = emptyList(),
        var createResult: ChromiumBrowserProfilesRepository.LifecycleResult =
            ChromiumBrowserProfilesRepository.LifecycleResult(),
        var activateResult: ChromiumBrowserProfilesRepository.LifecycleResult =
            ChromiumBrowserProfilesRepository.LifecycleResult(),
        var deleteResult: ChromiumBrowserProfilesRepository.LifecycleResult =
            ChromiumBrowserProfilesRepository.LifecycleResult(),
    ) : ChromiumBrowserProfilesRepository.LifecycleBridge {
        var listCalls = 0

        override fun listProfiles(
            currentProfile: Profile,
        ): List<BrowserProfilesRepository.Profile> {
            listCalls += 1
            return profiles
        }

        override fun createProfile(
            currentProfile: Profile,
            displayName: String,
            callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
        ) = callback(createResult)

        override fun activateProfile(
            currentProfile: Profile,
            profileId: String,
            callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
        ) = callback(activateResult)

        override fun deleteProfile(
            currentProfile: Profile,
            profileId: String,
            callback: (ChromiumBrowserProfilesRepository.LifecycleResult) -> Unit,
        ) = callback(deleteResult)
    }
}
