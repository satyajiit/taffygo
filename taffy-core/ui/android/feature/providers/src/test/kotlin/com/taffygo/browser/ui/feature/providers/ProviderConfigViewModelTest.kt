// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.credentials.ProviderProbeVerdict
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-415's three consequential paths: prove and store a key, remove a
 * credential, and move where Taffy's own requests go.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ProviderConfigViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val roster = FakeProviderRoster()
    private val credentials = FakeProviderCredentials()
    private val prober = FakeProviderProber()
    private var preferences = FakeProviderPreferences()
    private val errand = FakeErrandPage()
    private val analytics = RecordingProviderAnalytics()
    private val navigator = RecordingProviderNavigator()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a usable verdict stores the key and settles where requests go`() = runTest(dispatcher) {
        val viewModel = started()
        type(viewModel, "tk-live")

        viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
        runCurrent()

        assertEquals(listOf("tk-live"), credentials.saved)
        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            preferences.preferences.value.providerRoute,
        )
        assertEquals(ProviderKeyForm.Stage.CONNECTED, viewModel.state.value.keyForm?.stage)
        assertEquals("", viewModel.state.value.keyForm?.draft)
    }

    @Test
    fun `a definitive refusal keeps the draft and never offers to save anyway`() =
        runTest(dispatcher) {
            prober.verdict = ProviderProbeVerdict.AUTH
            val viewModel = started()
            type(viewModel, "tk-wrong")

            viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
            runCurrent()

            val form = viewModel.state.value.keyForm
            assertEquals(ProviderKeyProblem.KEY_REFUSED, form?.problem)
            assertEquals("tk-wrong", form?.draft)
            assertFalse(form?.offersSaveAnyway == true)
            assertTrue(credentials.saved.isEmpty())
        }

    @Test
    fun `an indefinite verdict offers to save anyway, and that save spends no second probe`() =
        runTest(dispatcher) {
            prober.verdict = ProviderProbeVerdict.NETWORK
            val viewModel = started()
            type(viewModel, "tk-unheard")

            viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
            runCurrent()
            assertTrue(viewModel.state.value.keyForm?.offersSaveAnyway == true)

            viewModel.onIntent(ProviderConfigIntent.SaveKeyAnyway, navigator)
            runCurrent()

            assertEquals(listOf("tk-unheard"), credentials.saved)
            assertEquals(1, prober.probed.size)
        }

    @Test
    fun `a draft of the wrong shape is refused before a probe is spent`() = runTest(dispatcher) {
        val viewModel = started()
        type(viewModel, "zz-nope")

        viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
        runCurrent()

        assertEquals(ProviderKeyProblem.PREFIX_MISMATCH, viewModel.state.value.keyForm?.problem)
        assertTrue(prober.probed.isEmpty())
        assertTrue(credentials.saved.isEmpty())
    }

    @Test
    fun `a store that refuses says so and connects nothing`() = runTest(dispatcher) {
        credentials.refuses = true
        val viewModel = started()
        type(viewModel, "tk-live")

        viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
        runCurrent()

        assertEquals(ProviderKeyProblem.STORE_FAILED, viewModel.state.value.keyForm?.problem)
        assertEquals(ProviderKeyForm.Stage.IDLE, viewModel.state.value.keyForm?.stage)
    }

    @Test
    fun `a probe the core will not run leaves the key unjudged and savable`() =
        runTest(dispatcher) {
            prober.refuses = true
            val viewModel = started()
            type(viewModel, "tk-live")

            viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
            runCurrent()

            assertEquals(
                ProviderKeyProblem.TEST_UNAVAILABLE,
                viewModel.state.value.keyForm?.problem,
            )
            assertTrue(viewModel.state.value.keyForm?.offersSaveAnyway == true)
        }

    /**
     * A provider whose catalog carries nothing to probe with is the one
     * indefinite answer nothing was sent for: what was judged is the list, not
     * the key. So the page names that rather than the transient "could not run
     * right now", and keeps both ways of saving open — for a provider that
     * serves its own list, saving is the only thing that ends the state
     * (decision 0098 section 1).
     */
    @Test
    fun `a provider with no model to probe with is named, and the save stays open`() =
        runTest(dispatcher) {
            prober.verdict = ProviderProbeVerdict.NO_MODEL_LISTED
            val viewModel = started()
            type(viewModel, "tk-unlisted")

            viewModel.onIntent(ProviderConfigIntent.SaveKey, navigator)
            runCurrent()
            val form = viewModel.state.value.keyForm
            assertEquals(ProviderKeyProblem.NO_MODEL_LISTED, form?.problem)
            assertTrue(form?.offersSaveAnyway == true)
            assertTrue(form?.actionable == true)
            assertTrue(credentials.saved.isEmpty())

            viewModel.onIntent(ProviderConfigIntent.SaveKeyAnyway, navigator)
            runCurrent()

            assertEquals(listOf("tk-unlisted"), credentials.saved)
            assertEquals(1, prober.probed.size)
        }

    @Test
    fun `a credential is removed only after the confirmation`() = runTest(dispatcher) {
        credentials.hold("keyed")
        val viewModel = started(stored = true)

        viewModel.onIntent(ProviderConfigIntent.ConfirmSignOut, navigator)
        runCurrent()
        assertTrue(credentials.forgotten.isEmpty())

        viewModel.onIntent(ProviderConfigIntent.AskSignOut, navigator)
        viewModel.onIntent(ProviderConfigIntent.ConfirmSignOut, navigator)
        runCurrent()

        assertEquals(listOf("keyed"), credentials.forgotten)
        assertFalse(viewModel.state.value.signingOut)
    }

    /**
     * The action is offered only where the product would not already be
     * naming this provider, and after the managed route left there is one
     * such state: the browser holds a credential handle the core's roster has
     * not echoed yet. That row is configured — so the page is not a set-up
     * form — and is not a standing candidate, because a credential of
     * uncertain usability never becomes a keyless route. Taking the default
     * there writes the route and touches nothing else.
     */
    @Test
    fun `taking the default is a route change and nothing else`() = runTest(dispatcher) {
        credentials.hold("keyed")
        val viewModel = started()
        assertEquals(ProviderDefaultChoice.OFFERED, viewModel.state.value.defaultChoice)

        viewModel.onIntent(ProviderConfigIntent.UseForTaffy, navigator)
        runCurrent()

        assertEquals(
            ProviderRoute.DIRECT_WITH_YOUR_KEY,
            preferences.preferences.value.providerRoute,
        )
    }

    @Test
    fun `the default cannot be taken where the product could not honour it`() =
        runTest(dispatcher) {
            val viewModel = started()

            viewModel.onIntent(ProviderConfigIntent.UseForTaffy, navigator)
            runCurrent()

            assertEquals(
                ProviderRoute.NOT_CONFIGURED,
                preferences.preferences.value.providerRoute,
            )
        }

    @Test
    fun `the sign-in and the models are two destinations, both scoped to this provider`() =
        runTest(dispatcher) {
            val viewModel = started()

            viewModel.onIntent(ProviderConfigIntent.StartSignIn, navigator)
            viewModel.onIntent(ProviderConfigIntent.ChangeModel, navigator)

            assertEquals(
                listOf(
                    TaffyDestination.ProviderSignIn("keyed"),
                    TaffyDestination.ModelSelection("keyed"),
                ),
                navigator.visited,
            )
        }

    @Test
    fun `the key page opens as an errand, and a catalog that named none opens nothing`() =
        runTest(dispatcher) {
            val viewModel = started()

            viewModel.onIntent(ProviderConfigIntent.OpenKeyPage, navigator)
            viewModel.onIntent(ProviderConfigIntent.OpenDocs, navigator)
            runCurrent()

            assertEquals(listOf("https://example.test/keys"), errand.opened)
            // Nothing navigated: the browser shows an errand as part of opening
            // it, and this page stays underneath with its draft key intact.
            assertEquals(emptyList<TaffyDestination>(), navigator.visited)
        }

    @Test
    fun `the screen records itself as SCR-415`() = runTest(dispatcher) {
        started().onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.ProviderConfig("keyed").screenId)),
            analytics.recorded,
        )
    }

    private fun TestScope.started(
        stored: Boolean = false,
        route: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
    ): ProviderConfigViewModel {
        roster.publish(
            providerRow(
                "keyed",
                authMethods = listOf(RosterAuthMethod.API_KEY),
                stored = if (stored) storedCredential() else null,
                presentation = ProviderPresentation(
                    keyPrefix = "tk-",
                    getKeyUrl = "https://example.test/keys",
                    docsUrl = null,
                ),
            ),
        )
        preferences = FakeProviderPreferences(route)
        val viewModel = ProviderConfigViewModel(
            roster,
            credentials,
            prober,
            preferences,
            errand,
            analytics,
            SavedStateHandle(mapOf(TaffyDestination.PROVIDER_ID to "keyed")),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        return viewModel
    }

    private fun TestScope.type(viewModel: ProviderConfigViewModel, draft: String) {
        viewModel.onIntent(ProviderConfigIntent.ChangeKey(draft), navigator)
        runCurrent()
    }
}
