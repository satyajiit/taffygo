// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModelStore
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.UnavailableVoiceInput
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.VoiceInput
import com.taffygo.browser.ui.core.ui.VoiceInputEvent
import com.taffygo.browser.ui.core.ui.VoiceInputSession
import com.taffygo.browser.ui.core.ui.VoiceTranscript
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class AddressBarVoiceViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    /**
     * A finished transcript is the request (screen SCR-709): it goes into the
     * box and its reading is chosen at once, with no review and no second tap.
     * Here the words read as a search, so they are committed and the box is
     * spent the way a typed reading spends it.
     */
    @Test
    fun `a finished transcript is confirmed and its reading chosen without a tap`() {
        val browser = RecordingBrowser()
        val voice = RecordingVoiceInput()
        val navigator = RecordingNavigator()
        val viewModel = addressBar(browser, SavedStateHandle(), voice)

        viewModel.onIntent(AddressBarIntent.StartVoiceInput, navigator)
        assertEquals(emptyList<String>(), browser.committed)
        voice.emit(VoiceInputEvent.Ready(transcript("find local trains")))
        dispatcher.scheduler.runCurrent()

        assertEquals(listOf("find local trains"), browser.committed)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
        assertEquals("", viewModel.state.value.input)
        assertEquals(VoiceEntryState.Closed, viewModel.state.value.voiceEntry)
    }

    @Test
    fun `cancel and destruction both close active microphone sessions`() {
        val voice = RecordingVoiceInput()
        val viewModel = addressBar(RecordingBrowser(), SavedStateHandle(), voice)

        viewModel.onIntent(AddressBarIntent.StartVoiceInput, RecordingNavigator())
        viewModel.onIntent(AddressBarIntent.CancelVoiceInput, RecordingNavigator())
        assertEquals(1, voice.closeCount)

        viewModel.onIntent(AddressBarIntent.StartVoiceInput, RecordingNavigator())
        val store = ViewModelStore()
        store.put("address", viewModel)
        store.clear()
        assertEquals(2, voice.closeCount)
    }

    @Test
    fun `a synchronous terminal result cannot leave its returned session active`() {
        val voice = SynchronousVoiceInput(
            VoiceInputEvent.Ready(transcript("find local trains")),
        )
        val viewModel = addressBar(RecordingBrowser(), SavedStateHandle(), voice)

        viewModel.onIntent(AddressBarIntent.StartVoiceInput, RecordingNavigator())

        // Submitted on the spot, and the session the adapter handed back was
        // closed rather than kept as if it were still listening.
        assertEquals(VoiceEntryState.Closed, viewModel.state.value.voiceEntry)
        assertEquals("", viewModel.state.value.input)
        assertEquals(1, voice.closeCount)
    }

    @Test
    fun `clear data command only opens its reviewed screen`() {
        val browser = RecordingBrowser()
        val navigator = RecordingNavigator()
        val viewModel = addressBar(browser, SavedStateHandle())

        viewModel.onIntent(
            AddressBarIntent.Choose(
                AddressBarInterpretation.BrowserCommand(
                    "clear browsing data",
                    AddressBarCommand.OPEN_CLEAR_BROWSING_DATA,
                ),
            ),
            navigator,
        )

        assertEquals(listOf(TaffyDestination.ClearBrowsingData), navigator.replaced)
        assertEquals(emptyList<String>(), browser.committed)
    }

    @Test
    fun `confirmed speech in a private tab never enters saved state`() {
        val handle = SavedStateHandle()
        val browser = RecordingBrowser(
            listOf(
                Tab(
                    TabId("private"),
                    "Private",
                    "secret.example",
                    isPrivate = true,
                    isSelected = true,
                ),
            ),
        )
        val voice = RecordingVoiceInput()
        val viewModel = addressBar(browser, handle, voice)

        viewModel.onIntent(AddressBarIntent.StartVoiceInput, RecordingNavigator())
        voice.emit(VoiceInputEvent.Ready(transcript("private search")))
        dispatcher.scheduler.runCurrent()

        // Spoken, confirmed and submitted, and at no point written down.
        assertEquals(listOf("private search"), browser.committed)
        assertNull(handle.get<String>("address_bar_input"))
        assertEquals(
            "",
            addressBar(browser, handle).state.value.input,
        )
    }

    private fun addressBar(
        browser: BrowserRepository,
        handle: SavedStateHandle = SavedStateHandle(),
        voice: VoiceInput = UnavailableVoiceInput,
    ): AddressBarViewModel = AddressBarViewModel(
        browser,
        NoAnalytics,
        handle,
        RecordingTaskRepository(),
        FixedReadiness(),
        voice,
    )

    private fun transcript(text: String): VoiceTranscript =
        requireNotNull(VoiceTranscript.bounded(text))

    private class RecordingVoiceInput : VoiceInput {
        var closeCount = 0
        private var callback: ((VoiceInputEvent) -> Unit)? = null

        override fun listen(onEvent: (VoiceInputEvent) -> Unit): VoiceInputSession {
            callback = onEvent
            return VoiceInputSession { closeCount++ }
        }

        fun emit(event: VoiceInputEvent) {
            callback?.invoke(event)
        }
    }

    private class SynchronousVoiceInput(private val event: VoiceInputEvent) : VoiceInput {
        var closeCount = 0

        override fun listen(onEvent: (VoiceInputEvent) -> Unit): VoiceInputSession {
            onEvent(event)
            return VoiceInputSession { closeCount++ }
        }
    }

    private class RecordingBrowser(initialTabs: List<Tab> = emptyList()) : BrowserRepository {
        val committed = mutableListOf<String>()
        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(initialTabs)
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
        override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
        override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
        override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
        override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)
        override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

        override fun resolve(input: String): AddressBarInterpretation =
            AddressBarInterpretation.Search(input)
        override fun suggestions(input: String): List<Suggestion> = emptyList()
        override suspend fun commit(interpretation: AddressBarInterpretation) {
            committed += interpretation.input
        }
        override fun dismissNotice() = Unit
        override suspend fun selectTab(id: TabId) = Unit
        override suspend fun closeTab(id: TabId) = Unit
        override suspend fun openTab(host: String, isPrivate: Boolean) = TabId("opened")
        override suspend fun goBack() = false
        override suspend fun goForward() = false
        override suspend fun reload() = Unit
        override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false
        override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit
        override suspend fun setFilteringEnabled(enabled: Boolean) = Unit
        override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = true
        override suspend fun flushFilteringCounts() = Unit
    }

    private class RecordingNavigator : TaffyNavigator {
        val replaced = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) {
            replaced += destination
        }
        override fun goBack() = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private data object NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}
