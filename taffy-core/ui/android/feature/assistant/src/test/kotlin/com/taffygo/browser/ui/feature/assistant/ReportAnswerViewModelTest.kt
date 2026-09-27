// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderModelView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.StoredCredentialView

@OptIn(ExperimentalCoroutinesApi::class)
class ReportAnswerViewModelTest {

    private val dispatcher = StandardTestDispatcher()
    private val base = RecordingComposerCoreApiClient().status.value

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `the providers a request could reach are named with the model chosen for each`() {
        val status = base.copy(
            provider_roster = listOf(
                row("spacexai", "SpaceXAI", usable = true, pinned = "grok-4"),
                row("openai", "OpenAI", usable = false, pinned = "gpt-5"),
                row("own", "My server", usable = false, origin = ProviderOriginView.CUSTOM),
                row("gone", "Gone pin", usable = true, pinned = "retired"),
            ),
            provider_models = listOf(model("spacexai", "grok-4", "Grok 4")),
        )

        assertEquals(
            listOf(
                ReportAnswerUiState.Provider("SpaceXAI", "Grok 4"),
                ReportAnswerUiState.Provider("My server", null),
                // A pin the roster no longer lists is no pin.
                ReportAnswerUiState.Provider("Gone pin", null),
            ),
            reportedProviders(status),
        )
    }

    @Test
    fun `a core that has not published a whole projection names no provider`() {
        val status = base.copy(
            availability = CoreAvailability.STARTING,
            provider_roster = listOf(row("spacexai", "SpaceXAI", usable = true)),
        )

        assertEquals(emptyList<ReportAnswerUiState.Provider>(), reportedProviders(status))
    }

    @Test
    fun `opening the sheet reads the providers the core holds now`() {
        val core = coreWith(
            base.copy(provider_roster = listOf(row("spacexai", "SpaceXAI", usable = true))),
        )
        val viewModel = ReportAnswerViewModel(core, TabBrowser(TabId("tab-1")))

        viewModel.onIntent(ReportAnswerIntent.Open(answer("text")), RecordingNavigator())

        assertEquals(
            listOf(ReportAnswerUiState.Provider("SpaceXAI", null)),
            viewModel.state.value.report?.providers,
        )
    }

    @Test
    fun `a public issue opens the bug form in a tab with only the title`() = runTest(dispatcher) {
        val browser = TabBrowser(TabId("tab-1"))
        val navigator = RecordingNavigator()
        val viewModel = ReportAnswerViewModel(coreWith(base), browser)
        viewModel.onIntent(ReportAnswerIntent.Open(answer("private words")), navigator)

        viewModel.onIntent(ReportAnswerIntent.OpenPublicIssue("A title"), navigator)
        runCurrent()

        assertEquals(listOf(TaffyProjectContact.newIssueAddress("A title")), browser.opened)
        assertEquals(listOf<TaffyDestination>(TaffyDestination.BrowserMain), navigator.destinations)
        assertNull(viewModel.state.value.report)
    }

    @Test
    fun `a tab the browser could not open leaves the person where they were`() =
        runTest(dispatcher) {
            val navigator = RecordingNavigator()
            val viewModel = ReportAnswerViewModel(coreWith(base), TabBrowser(TabId("")))

            viewModel.onIntent(ReportAnswerIntent.OpenPublicIssue("A title"), navigator)
            runCurrent()

            assertEquals(emptyList<TaffyDestination>(), navigator.destinations)
        }

    private fun coreWith(status: CoreStatus): CoreApiClient =
        object : CoreApiClient by RecordingComposerCoreApiClient() {
            override val status: StateFlow<CoreStatus> = MutableStateFlow(status)
        }

    private class TabBrowser(
        private val tab: TabId,
        private val base: TaskBrowserTestRepository = TaskBrowserTestRepository(),
    ) : BrowserRepository by base {
        val opened = mutableListOf<String>()

        override suspend fun openTab(host: String, isPrivate: Boolean): TabId {
            opened += host
            return tab
        }
    }

    private fun answer(text: String) = TaskAnswerProjection(
        segments = listOf(text),
        isStreaming = false,
        isIncomplete = false,
        isTruncated = false,
    )

    private fun row(
        id: String,
        name: String,
        usable: Boolean,
        pinned: String? = null,
        origin: ProviderOriginView = ProviderOriginView.CATALOG,
    ) = ProviderRosterEntry(
        provider_id = id,
        display_name = name,
        origin = origin,
        auth_methods = listOf(ProviderAuthMethodView.API_KEY),
        stored = StoredCredentialView(
            auth_method = ProviderAuthMethodView.API_KEY,
            state = if (usable) {
                ProviderCredentialStateView.USABLE
            } else {
                ProviderCredentialStateView.NEEDS_SIGN_IN
            },
            subscription_backed = false,
            account_label = null,
            plan_label = null,
        ),
        signing_in = false,
        enabled = true,
        endpoint_host = null,
        configurable = false,
        endpoint_changed = false,
        catalog_layer = CatalogLayerView.EMBEDDED_BASELINE,
        selected_model_id = pinned,
        thinking = null,
        presentation = null,
        endpoint_base = null,
        last_refusal = null,
        model_count = 1u,
        subscription = false,
        refused_endpoint_host = null,
    )

    private fun model(provider: String, id: String, name: String) = ProviderModelView(
        provider_id = provider,
        model_id = id,
        display_name = name,
        context_window = 0uL,
        max_output_tokens = 0uL,
        reasoning = false,
        tool_calling = false,
        roles = emptyList(),
        input_modalities = emptyList(),
        thinking_levels = emptyList(),
    )
}
