// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.feature.browsing.SiteInfoRepository
import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumSiteInfoRepositoryTest {

    @Test
    fun `availability and state come only from the selected committed host`() {
        val environment = FakeEnvironment()
        val settings = FakeSettings(
            SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = listOf(
                    SiteSettingsRepository.Entry(
                        host = "docs.example",
                        changedPermissionCount = 3,
                        changedCapabilities = setOf(
                            SiteSettingsRepository.Capability.MICROPHONE,
                            SiteSettingsRepository.Capability.LOCATION,
                        ),
                    ),
                ),
            ),
        )
        val repository = repository(environment, settings)

        assertFalse(repository.desktopSiteAvailable)
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            repository.permissionsFor("docs.example"),
        )
        environment.host = "docs.example"
        environment.page.currentDesktopSite = true
        assertTrue(repository.desktopSiteAvailable)
        assertTrue(repository.isDesktopSite("docs.example"))
        assertFalse(repository.isDesktopSite("other.example"))
        assertEquals(
            SiteInfoRepository.PermissionState.Changed(
                changedCount = 3,
                capabilities = listOf(
                    SiteInfoRepository.PermissionCapability.LOCATION,
                    SiteInfoRepository.PermissionCapability.MICROPHONE,
                ),
            ),
            repository.permissionsFor("docs.example"),
        )
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            repository.permissionsFor("other.example"),
        )
        repository.close()
    }

    @Test
    fun `loading unavailable and ready empty are never conflated`() {
        val environment = FakeEnvironment(host = "docs.example")
        val settings = FakeSettings(SiteSettingsRepository.Snapshot.Loading)
        val repository = repository(environment, settings)

        assertEquals(
            SiteInfoRepository.PermissionState.Loading,
            repository.permissionsFor("docs.example"),
        )
        val loadingRevision = repository.permissionRevision.value
        settings.publish(SiteSettingsRepository.Snapshot.Unavailable)
        assertTrue(repository.permissionRevision.value > loadingRevision)
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            repository.permissionsFor("docs.example"),
        )
        settings.publish(SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()))
        assertEquals(
            SiteInfoRepository.PermissionState.Empty,
            repository.permissionsFor("docs.example"),
        )
        repository.close()
    }

    @Test
    fun `inconsistent capability counts fail closed instead of inventing facts`() {
        val environment = FakeEnvironment(host = "docs.example")
        val settings = FakeSettings(
            SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = listOf(
                    SiteSettingsRepository.Entry(
                        host = "docs.example",
                        changedPermissionCount = 1,
                        changedCapabilities = setOf(
                            SiteSettingsRepository.Capability.CAMERA,
                            SiteSettingsRepository.Capability.MICROPHONE,
                        ),
                    ),
                ),
            ),
        )
        val repository = repository(environment, settings)

        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            repository.permissionsFor("docs.example"),
        )
        repository.close()
    }

    @Test
    fun `a host change before dispatch prevents a stale desktop action`() = runBlocking {
        val environment = FakeEnvironment(host = "first.example")
        val repository = repository(environment)

        assertFalse(repository.isDesktopSite("first.example"))
        environment.host = "second.example"
        repository.setDesktopSite("first.example", enabled = true)
        assertFalse(environment.page.currentDesktopSite)
        assertFalse(environment.page.wasSet)

        repository.setDesktopSite("second.example", enabled = true)
        assertTrue(environment.page.currentDesktopSite)
        assertTrue(environment.page.wasSet)
        repository.close()
    }

    @Test
    fun `closed repository exposes and changes nothing`() = runBlocking {
        val environment = FakeEnvironment(host = "example.test")
        val settings = FakeSettings(changed("example.test"))
        val repository = repository(environment, settings)

        repository.close()
        repository.close()
        assertFalse(repository.desktopSiteAvailable)
        assertFalse(repository.isDesktopSite("example.test"))
        assertEquals(
            SiteInfoRepository.PermissionState.Unavailable,
            repository.permissionsFor("example.test"),
        )
        repository.setDesktopSite("example.test", enabled = true)
        assertEquals(
            SiteInfoRepository.PermissionResetResult.UNAVAILABLE,
            repository.resetPermissions("example.test"),
        )
        assertFalse(environment.page.wasSet)
        assertEquals(emptyList<String>(), settings.resetHosts)
    }

    @Test
    fun `desktop mutation uses the injected window main dispatcher`() = runBlocking {
        val main = RecordingDispatcher()
        val environment = FakeEnvironment(host = "example.test")
        val settings = FakeSettings()
        val repository = repository(environment, settings, testDispatchers(main))

        repository.setDesktopSite("example.test", enabled = true)
        repository.refreshPermissions("example.test")

        assertTrue(environment.page.wasSet)
        assertEquals(1, settings.refreshCount)
        assertTrue(main.dispatchCount > 0)
        repository.close()
    }

    @Test
    fun `permission refresh and reset re-resolve the exact host`() = runBlocking {
        val environment = FakeEnvironment(host = "first.example")
        val settings = FakeSettings(changed("first.example"))
        val repository = repository(environment, settings)

        repository.refreshPermissions("other.example")
        environment.host = "second.example"
        val result = repository.resetPermissions("first.example")

        assertEquals(0, settings.refreshCount)
        assertEquals(SiteInfoRepository.PermissionResetResult.REFUSED, result)
        assertEquals(emptyList<String>(), settings.resetHosts)
        repository.close()
    }

    @Test
    fun `applied reset requires and publishes an empty read-back`() = runBlocking {
        val environment = FakeEnvironment(host = "docs.example")
        val settings = FakeSettings(changed("docs.example")).apply {
            resetResult = SiteSettingsRepository.UpdateResult.APPLIED
            snapshotAfterReset = SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList())
        }
        val repository = repository(environment, settings)

        val result = repository.resetPermissions("docs.example")

        assertEquals(SiteInfoRepository.PermissionResetResult.APPLIED, result)
        assertEquals(listOf("docs.example"), settings.resetHosts)
        assertEquals(
            SiteInfoRepository.PermissionState.Empty,
            repository.permissionsFor("docs.example"),
        )
        repository.close()
    }

    @Test
    fun `a contradictory applied result fails closed and keeps current facts`() = runBlocking {
        val environment = FakeEnvironment(host = "docs.example")
        val settings = FakeSettings(changed("docs.example")).apply {
            resetResult = SiteSettingsRepository.UpdateResult.APPLIED
        }
        val repository = repository(environment, settings)

        val result = repository.resetPermissions("docs.example")

        assertEquals(SiteInfoRepository.PermissionResetResult.FAILED, result)
        assertTrue(
            repository.permissionsFor("docs.example")
                is SiteInfoRepository.PermissionState.Changed,
        )
        repository.close()
    }

    private fun repository(
        environment: FakeEnvironment,
        settings: FakeSettings = FakeSettings(),
        dispatchers: AppDispatchers = testDispatchers(),
    ): ChromiumSiteInfoRepository = ChromiumSiteInfoRepository(
        environment,
        dispatchers,
        ChromiumSiteInfoPermissionAdapter(settings, CoroutineScope(Dispatchers.Unconfined)),
    )

    private fun testDispatchers(
        main: CoroutineDispatcher = Dispatchers.Unconfined,
    ): AppDispatchers = object : AppDispatchers {
        override val main = main
        override val default = Dispatchers.Unconfined
        override val io = Dispatchers.Unconfined
    }

    private class RecordingDispatcher : CoroutineDispatcher() {
        var dispatchCount = 0

        override fun dispatch(context: CoroutineContext, block: Runnable) {
            dispatchCount += 1
            block.run()
        }
    }

    private class FakeSettings(
        initial: SiteSettingsRepository.Snapshot =
            SiteSettingsRepository.Snapshot.Ready(emptyList(), emptyList()),
    ) : SiteSettingsRepository {
        private val mutableSnapshot = MutableStateFlow(initial)
        override val snapshot: StateFlow<SiteSettingsRepository.Snapshot> = mutableSnapshot
        var refreshCount = 0
        val resetHosts = mutableListOf<String>()
        var resetResult = SiteSettingsRepository.UpdateResult.REFUSED
        var snapshotAfterReset: SiteSettingsRepository.Snapshot? = null

        fun publish(value: SiteSettingsRepository.Snapshot) {
            mutableSnapshot.value = value
        }

        override fun refresh() {
            refreshCount += 1
        }

        override suspend fun setDefault(
            capability: SiteSettingsRepository.Capability,
            enabled: Boolean,
        ) = SiteSettingsRepository.UpdateResult.UNSUPPORTED

        override suspend fun resetSite(host: String): SiteSettingsRepository.UpdateResult {
            resetHosts += host
            snapshotAfterReset?.let(::publish)
            return resetResult
        }
    }

    private class FakeEnvironment(
        var host: String? = null,
    ) : ChromiumSiteInfoRepository.Environment {
        val page = FakePage()

        override fun selectedPage(expectedHost: String?): ChromiumSiteInfoRepository.Page? {
            val current = host ?: return null
            return page.takeIf { expectedHost == null || expectedHost == current }
        }
    }

    private class FakePage : ChromiumSiteInfoRepository.Page {
        var currentDesktopSite: Boolean = false
        override val desktopSite: Boolean
            get() = currentDesktopSite
        var wasSet = false

        override fun setDesktopSite(enabled: Boolean) {
            wasSet = true
            currentDesktopSite = enabled
        }
    }

    private companion object {
        fun changed(host: String): SiteSettingsRepository.Snapshot.Ready =
            SiteSettingsRepository.Snapshot.Ready(
                defaults = emptyList(),
                sites = listOf(
                    SiteSettingsRepository.Entry(
                        host = host,
                        changedPermissionCount = 1,
                        changedCapabilities = setOf(SiteSettingsRepository.Capability.CAMERA),
                    ),
                ),
            )
    }
}
