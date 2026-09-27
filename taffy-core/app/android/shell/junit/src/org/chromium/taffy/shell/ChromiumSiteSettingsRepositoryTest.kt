// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.settings.SiteSettingsRepository
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumSiteSettingsRepositoryTest {
    @Test
    fun `site exception takes precedence and survives a default mutation`() = runBlocking {
        val backend = FakeBackend(
            SiteSettingsRepository.Capability.CAMERA to value(enabled = true),
        )
        val repository = ChromiumSiteSettingsRepository(backend) {}
        backend.answer(
            ChromiumSiteSettingsRepository.SiteFact(
                host = "camera.example.test",
                changedPermissionCount = 1,
                changedCapabilities = setOf(SiteSettingsRepository.Capability.CAMERA),
            ),
        )
        val before = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertEquals(
            SiteSettingsRepository.SettingSource.SITE_EXCEPTION,
            before.sourceFor("camera.example.test", SiteSettingsRepository.Capability.CAMERA),
        )
        assertEquals(
            SiteSettingsRepository.SettingSource.GLOBAL_DEFAULT,
            before.sourceFor("other.example.test", SiteSettingsRepository.Capability.CAMERA),
        )

        assertEquals(
            SiteSettingsRepository.UpdateResult.APPLIED,
            repository.setDefault(SiteSettingsRepository.Capability.CAMERA, enabled = false),
        )

        val after = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertFalse(after.defaults.single().enabled)
        assertEquals(
            SiteSettingsRepository.SettingSource.SITE_EXCEPTION,
            after.sourceFor("camera.example.test", SiteSettingsRepository.Capability.CAMERA),
        )
    }

    @Test
    fun `unsupported capability is omitted and never written`() = runBlocking {
        val backend = FakeBackend(
            SiteSettingsRepository.Capability.CAMERA to value(enabled = true),
        )
        val repository = ChromiumSiteSettingsRepository(backend) {}
        backend.answer()

        assertEquals(
            SiteSettingsRepository.UpdateResult.UNSUPPORTED,
            repository.setDefault(SiteSettingsRepository.Capability.SENSORS, enabled = false),
        )
        assertTrue(backend.writes.isEmpty())
        val ready = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertEquals(listOf(SiteSettingsRepository.Capability.CAMERA), ready.defaults.map { it.capability })
    }

    @Test
    fun `failed read back keeps the live value and reports failure`() = runBlocking {
        val backend = FakeBackend(
            SiteSettingsRepository.Capability.CAMERA to value(enabled = true),
        ).apply { writesStick = false }
        val repository = ChromiumSiteSettingsRepository(backend) {}
        backend.answer()

        assertEquals(
            SiteSettingsRepository.UpdateResult.FAILED,
            repository.setDefault(SiteSettingsRepository.Capability.CAMERA, enabled = false),
        )

        val ready = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertTrue(ready.defaults.single().enabled)
        assertEquals(listOf(SiteSettingsRepository.Capability.CAMERA to false), backend.writes)
    }

    @Test
    fun `close withdraws state and ignores a late fetch reply`() = runBlocking {
        val backend = FakeBackend(
            SiteSettingsRepository.Capability.CAMERA to value(enabled = true),
        )
        val repository = ChromiumSiteSettingsRepository(backend) {}

        repository.close()
        repository.close()
        backend.answer(
            ChromiumSiteSettingsRepository.SiteFact(
                "late.example.test",
                1,
                setOf(SiteSettingsRepository.Capability.CAMERA),
            ),
        )

        assertEquals(1, backend.closeCount)
        assertTrue(repository.snapshot.value is SiteSettingsRepository.Snapshot.Unavailable)
        assertEquals(
            SiteSettingsRepository.UpdateResult.UNAVAILABLE,
            repository.setDefault(SiteSettingsRepository.Capability.CAMERA, enabled = false),
        )
    }

    @Test
    fun `duplicate site facts aggregate in one bounded count without overflow`() {
        val backend = FakeBackend()
        val repository = ChromiumSiteSettingsRepository(backend) {}
        backend.answer(
            ChromiumSiteSettingsRepository.SiteFact(
                host = "large.example.test",
                changedPermissionCount = Int.MAX_VALUE,
                changedCapabilities = setOf(SiteSettingsRepository.Capability.CAMERA),
            ),
            ChromiumSiteSettingsRepository.SiteFact(
                host = "large.example.test",
                changedPermissionCount = 2,
                changedCapabilities = setOf(SiteSettingsRepository.Capability.MICROPHONE),
            ),
        )

        val ready = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertEquals(
            listOf(
                SiteSettingsRepository.Entry(
                    host = "large.example.test",
                    changedPermissionCount = Int.MAX_VALUE,
                    changedCapabilities = setOf(
                        SiteSettingsRepository.Capability.CAMERA,
                        SiteSettingsRepository.Capability.MICROPHONE,
                    ),
                ),
            ),
            ready.sites,
        )
    }

    @Test
    fun `site reset is applied only after the exact host disappears on read back`() = runBlocking {
        val backend = FakeBackend()
        val repository = ChromiumSiteSettingsRepository(backend) {}
        backend.answer(
            ChromiumSiteSettingsRepository.SiteFact(
                "camera.example.test",
                1,
                setOf(SiteSettingsRepository.Capability.CAMERA),
            ),
        )

        val result = async(start = CoroutineStart.UNDISPATCHED) {
            repository.resetSite("camera.example.test")
        }
        assertEquals(listOf("camera.example.test"), backend.resets)
        backend.answer()

        assertEquals(SiteSettingsRepository.UpdateResult.APPLIED, result.await())
        val ready = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertTrue(ready.sites.isEmpty())
    }

    @Test
    fun `site reset reports failure and publishes permissions left by read back`() = runBlocking {
        val backend = FakeBackend()
        val repository = ChromiumSiteSettingsRepository(backend) {}
        val fact = ChromiumSiteSettingsRepository.SiteFact(
            "managed.example.test",
            1,
            setOf(SiteSettingsRepository.Capability.CAMERA),
        )
        backend.answer(fact)

        val result = async(start = CoroutineStart.UNDISPATCHED) {
            repository.resetSite("managed.example.test")
        }
        backend.answer(fact)

        assertEquals(SiteSettingsRepository.UpdateResult.FAILED, result.await())
        val ready = repository.snapshot.value as SiteSettingsRepository.Snapshot.Ready
        assertEquals(listOf("managed.example.test"), ready.sites.map { it.host })
    }

    private class FakeBackend(
        vararg initial: Pair<SiteSettingsRepository.Capability, ChromiumSiteSettingsRepository.DefaultValue>,
    ) : ChromiumSiteSettingsRepository.Backend {
        private val defaults = initial.toMap().toMutableMap()
        private lateinit var callback: (List<ChromiumSiteSettingsRepository.SiteFact>?) -> Unit
        val writes = mutableListOf<Pair<SiteSettingsRepository.Capability, Boolean>>()
        var writesStick = true
        var closeCount = 0
        var resetResult = true
        val resets = mutableListOf<String>()

        override fun readDefault(
            capability: SiteSettingsRepository.Capability,
        ): ChromiumSiteSettingsRepository.DefaultValue? = defaults[capability]

        override fun writeDefault(
            capability: SiteSettingsRepository.Capability,
            enabled: Boolean,
        ) {
            writes += capability to enabled
            if (writesStick) defaults[capability] = defaults.getValue(capability).copy(enabled = enabled)
        }

        override fun fetchSites(
            callback: (List<ChromiumSiteSettingsRepository.SiteFact>?) -> Unit,
        ) {
            this.callback = callback
        }

        override fun resetSite(host: String): Boolean {
            resets += host
            return resetResult
        }

        fun answer(vararg sites: ChromiumSiteSettingsRepository.SiteFact) {
            callback(sites.toList())
        }

        override fun close() {
            closeCount++
        }
    }

    private companion object {
        fun value(enabled: Boolean) = ChromiumSiteSettingsRepository.DefaultValue(
            enabled = enabled,
            userModifiable = true,
        )
    }
}
