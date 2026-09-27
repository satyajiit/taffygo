// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Before
import org.junit.Test

class SitePermissionsViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `opening refreshes and reset reports progress plus verified read-back`() =
        runTest(dispatcher) {
            val gate = CompletableDeferred<Unit>()
            val siteInfo = RecordingSiteInfoRepository(resetGate = gate)
            val viewModel = browserMainViewModel(
                browser = selectedBrowser(),
                siteInfo = siteInfo,
            )
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(BrowserMainIntent.OpenSiteFiltering, NoNavigation())
            runCurrent()
            assertEquals(listOf(HOST), siteInfo.refreshedHosts)
            assertEquals(siteInfo.permissions, viewModel.state.value.siteFiltering.permissions)

            viewModel.onIntent(BrowserMainIntent.RequestSitePermissionReset, NoNavigation())
            runCurrent()
            assertEquals(emptyList<String>(), siteInfo.resetHosts)
            assertEquals(true, viewModel.state.value.siteFiltering.permissionResetConfirmation)
            viewModel.onIntent(BrowserMainIntent.ConfirmSitePermissionReset, NoNavigation())
            runCurrent()
            assertEquals(listOf(HOST), siteInfo.resetHosts)
            assertEquals(
                SiteFilteringUiState.ActionProgress.RUNNING,
                viewModel.state.value.siteFiltering.permissionReset,
            )

            siteInfo.nextPermissions = SiteInfoRepository.PermissionState.Empty
            gate.complete(Unit)
            runCurrent()

            assertEquals(
                SiteInfoRepository.PermissionState.Empty,
                viewModel.state.value.siteFiltering.permissions,
            )
            assertEquals(
                SiteFilteringUiState.ActionProgress.SUCCEEDED,
                viewModel.state.value.siteFiltering.permissionReset,
            )
        }

    @Test
    fun `failed reset keeps the read-back and says it failed`() = runTest(dispatcher) {
        val siteInfo = RecordingSiteInfoRepository(
            resetResult = SiteInfoRepository.PermissionResetResult.FAILED,
        )
        val viewModel = browserMainViewModel(
            browser = selectedBrowser(),
            siteInfo = siteInfo,
        )
        backgroundScope.launch { viewModel.state.collect {} }
        viewModel.onIntent(BrowserMainIntent.OpenSiteFiltering, NoNavigation())
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.RequestSitePermissionReset, NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.ConfirmSitePermissionReset, NoNavigation())
        runCurrent()

        assertEquals(siteInfo.permissions, viewModel.state.value.siteFiltering.permissions)
        assertEquals(
            SiteFilteringUiState.ActionProgress.FAILED,
            viewModel.state.value.siteFiltering.permissionReset,
        )
    }

    @Test
    fun `dismissing confirmation keeps choices and sends no reset`() = runTest(dispatcher) {
        val siteInfo = RecordingSiteInfoRepository()
        val viewModel = browserMainViewModel(
            browser = selectedBrowser(),
            siteInfo = siteInfo,
        )
        backgroundScope.launch { viewModel.state.collect {} }
        viewModel.onIntent(BrowserMainIntent.OpenSiteFiltering, NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.ConfirmSitePermissionReset, NoNavigation())
        runCurrent()
        assertEquals(emptyList<String>(), siteInfo.resetHosts)
        viewModel.onIntent(BrowserMainIntent.RequestSitePermissionReset, NoNavigation())
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.DismissSitePermissionReset, NoNavigation())
        runCurrent()

        assertFalse(viewModel.state.value.siteFiltering.permissionResetConfirmation)
        assertEquals(emptyList<String>(), siteInfo.resetHosts)
        assertEquals(
            SiteInfoRepository.PermissionState.Changed(
                changedCount = 2,
                capabilities = listOf(
                    SiteInfoRepository.PermissionCapability.CAMERA,
                    SiteInfoRepository.PermissionCapability.MICROPHONE,
                ),
            ),
            viewModel.state.value.siteFiltering.permissions,
        )
    }

    @Test
    fun `closing the sheet withdraws a late reset result`() = runTest(dispatcher) {
        val gate = CompletableDeferred<Unit>()
        val siteInfo = RecordingSiteInfoRepository(resetGate = gate)
        val viewModel = browserMainViewModel(
            browser = selectedBrowser(),
            siteInfo = siteInfo,
        )
        backgroundScope.launch { viewModel.state.collect {} }
        viewModel.onIntent(BrowserMainIntent.OpenSiteFiltering, NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.RequestSitePermissionReset, NoNavigation())
        runCurrent()
        viewModel.onIntent(BrowserMainIntent.ConfirmSitePermissionReset, NoNavigation())
        runCurrent()

        viewModel.onIntent(BrowserMainIntent.DismissSiteFiltering, NoNavigation())
        gate.complete(Unit)
        runCurrent()

        assertFalse(viewModel.state.value.siteFilteringOpen)
        assertEquals(
            SiteFilteringUiState.ActionProgress.IDLE,
            viewModel.state.value.siteFiltering.permissionReset,
        )
    }

    private fun selectedBrowser() = FakeBrowser(
        tabs = listOf(
            Tab(
                id = TabId("selected"),
                title = "News",
                host = HOST,
                isSelected = true,
            ),
        ),
        host = HOST,
        title = "News",
    )

    private class RecordingSiteInfoRepository(
        private val resetResult: SiteInfoRepository.PermissionResetResult =
            SiteInfoRepository.PermissionResetResult.APPLIED,
        private val resetGate: CompletableDeferred<Unit>? = null,
    ) : SiteInfoRepository {
        override val desktopSiteAvailable = true
        private val mutableRevision = MutableStateFlow(0L)
        override val permissionRevision: StateFlow<Long> = mutableRevision
        var permissions: SiteInfoRepository.PermissionState =
            SiteInfoRepository.PermissionState.Changed(
                changedCount = 2,
                capabilities = listOf(
                    SiteInfoRepository.PermissionCapability.CAMERA,
                    SiteInfoRepository.PermissionCapability.MICROPHONE,
                ),
            )
            private set
        var nextPermissions: SiteInfoRepository.PermissionState? = null
        val refreshedHosts = mutableListOf<String>()
        val resetHosts = mutableListOf<String>()

        override fun isDesktopSite(host: String) = false

        override fun permissionsFor(host: String) = permissions

        override suspend fun refreshPermissions(host: String) {
            refreshedHosts += host
        }

        override suspend fun setDesktopSite(host: String, enabled: Boolean) = Unit

        override suspend fun resetPermissions(
            host: String,
        ): SiteInfoRepository.PermissionResetResult {
            resetHosts += host
            resetGate?.await()
            nextPermissions?.let { permissions = it }
            mutableRevision.value += 1L
            return resetResult
        }
    }

    private companion object {
        const val HOST = "news.example.test"
    }
}
