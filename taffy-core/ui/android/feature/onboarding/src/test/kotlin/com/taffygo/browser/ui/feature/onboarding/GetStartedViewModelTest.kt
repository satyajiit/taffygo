// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.preferences.InMemoryLocalProfileRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
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

/**
 * Screen SCR-007 writes once and hands off, and the hand-off is the part that
 * cannot be got wrong quietly.
 *
 * SCR-004 is the sequence's last screen and the only place that records the
 * first run as finished. A Get started that navigated anywhere else would
 * reach a browser with `onboardingCompleted` still false, and the shell would
 * restart the whole sequence on the next launch — with every gate green,
 * because nothing else in the tree states the order.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class GetStartedViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `continuing stores both answers and goes on to AI setup`() = runTest(dispatcher) {
        val profile = InMemoryLocalProfileRepository()
        val navigator = RecordingNavigator()
        val viewModel = GetStartedViewModel(profile, NoAnalytics())

        viewModel.onIntent(GetStartedIntent.EditName("  Ada Lovelace "), navigator)
        viewModel.onIntent(GetStartedIntent.ChooseAvatar(LocalAvatar.of("b3")), navigator)
        viewModel.onIntent(GetStartedIntent.Continue, navigator)
        runCurrent()

        assertEquals("Ada Lovelace", profile.profile.value.displayName)
        assertEquals(LocalAvatar.of("b3"), profile.profile.value.avatar)
        assertEquals(listOf(TaffyDestination.AiSetup), navigator.visited)
    }

    /**
     * Declining both is a complete answer. The monogram is written rather than
     * left alone, because it has no stored id: "chose the monogram" and "never
     * answered" are the same absent value, and only a write settles it.
     */
    @Test
    fun `continuing with nothing answered stores no name and still goes on`() =
        runTest(dispatcher) {
            val profile = InMemoryLocalProfileRepository()
            val navigator = RecordingNavigator()
            val viewModel = GetStartedViewModel(profile, NoAnalytics())

            viewModel.onIntent(GetStartedIntent.Continue, navigator)
            runCurrent()

            assertNull(profile.profile.value.displayName)
            assertEquals(LocalAvatar.Monogram, profile.profile.value.avatar)
            assertEquals(listOf(TaffyDestination.AiSetup), navigator.visited)
        }

    @Test
    fun `a name of spaces is stored as no name at all`() = runTest(dispatcher) {
        val profile = InMemoryLocalProfileRepository()
        val navigator = RecordingNavigator()
        val viewModel = GetStartedViewModel(profile, NoAnalytics())

        viewModel.onIntent(GetStartedIntent.EditName("   "), navigator)
        viewModel.onIntent(GetStartedIntent.Continue, navigator)
        runCurrent()

        assertNull(profile.profile.value.displayName)
    }

    /** Nothing here finishes the first run; SCR-004 is the only screen that does. */
    @Test
    fun `this screen never restarts into the browser`() = runTest(dispatcher) {
        val profile = InMemoryLocalProfileRepository()
        val navigator = RecordingNavigator()
        val viewModel = GetStartedViewModel(profile, NoAnalytics())

        viewModel.onIntent(GetStartedIntent.Continue, navigator)
        runCurrent()

        assertEquals(emptyList<TaffyDestination>(), navigator.restarted)
        assertEquals(emptyList<TaffyDestination>(), navigator.replaced)
    }

    /**
     * A second tap must not write twice or push AI setup twice. A large
     * primary button on a slow phone is tapped twice.
     */
    @Test
    fun `a second continue is ignored`() = runTest(dispatcher) {
        val profile = InMemoryLocalProfileRepository()
        val navigator = RecordingNavigator()
        val viewModel = GetStartedViewModel(profile, NoAnalytics())

        viewModel.onIntent(GetStartedIntent.EditName("Priya"), navigator)
        viewModel.onIntent(GetStartedIntent.Continue, navigator)
        viewModel.onIntent(GetStartedIntent.Continue, navigator)
        viewModel.onIntent(GetStartedIntent.EditName("Someone else"), navigator)
        runCurrent()

        assertEquals(listOf(TaffyDestination.AiSetup), navigator.visited)
        assertEquals("Priya", profile.profile.value.displayName)
    }

    private class RecordingNavigator : TaffyNavigator {
        val visited = mutableListOf<TaffyDestination>()
        val restarted = mutableListOf<TaffyDestination>()
        val replaced = mutableListOf<TaffyDestination>()

        override fun goTo(destination: TaffyDestination) {
            visited += destination
        }

        override fun restart(destination: TaffyDestination) {
            restarted += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            replaced += destination
        }

        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
