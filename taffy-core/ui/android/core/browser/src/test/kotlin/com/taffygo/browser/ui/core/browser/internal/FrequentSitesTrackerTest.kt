// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

internal class FrequentSitesTrackerTest {

    @Test
    fun `the first snapshot is a baseline and counts nothing`() = runTest {
        val tabs = MutableStateFlow(
            listOf(
                tab("tab_1", host = "docs.taffy.test"),
                tab("tab_2", host = "news.taffy.test"),
            ),
        )
        val recorder = RecordingRepository()

        FrequentSitesTracker(tabs, recorder).start(backgroundScope)
        runCurrent()

        assertEquals(emptyList<String>(), recorder.visits)
    }

    @Test
    fun `a host commit after the baseline is one visit`() = runTest {
        val tabs = MutableStateFlow(listOf(tab("tab_1", host = "")))
        val recorder = RecordingRepository()
        FrequentSitesTracker(tabs, recorder).start(backgroundScope)
        runCurrent()

        tabs.value = listOf(tab("tab_1", host = "docs.taffy.test", title = "Docs"))
        runCurrent()

        assertEquals(listOf("docs.taffy.test"), recorder.visits)
    }

    @Test
    fun `a new tab arriving somewhere is counted and moving on is counted again`() = runTest {
        val tabs = MutableStateFlow(listOf(tab("tab_1", host = "docs.taffy.test")))
        val recorder = RecordingRepository()
        FrequentSitesTracker(tabs, recorder).start(backgroundScope)
        runCurrent()

        tabs.value = tabs.value + tab("tab_2", host = "news.taffy.test")
        runCurrent()
        tabs.value = listOf(
            tab("tab_1", host = "docs.taffy.test"),
            tab("tab_2", host = "mail.taffy.test"),
        )
        runCurrent()

        assertEquals(listOf("news.taffy.test", "mail.taffy.test"), recorder.visits)
    }

    @Test
    fun `private tabs and Taffy tabs and blank hosts leave no trace`() = runTest {
        val tabs = MutableStateFlow(listOf(tab("tab_1", host = "docs.taffy.test")))
        val recorder = RecordingRepository()
        FrequentSitesTracker(tabs, recorder).start(backgroundScope)
        runCurrent()

        tabs.value = tabs.value +
            tab("tab_2", host = "secret.taffy.test", isPrivate = true) +
            tab("tab_3", host = "task.taffy.test", isTaffyTab = true) +
            tab("tab_4", host = "")
        runCurrent()

        assertEquals(emptyList<String>(), recorder.visits)
    }

    @Test
    fun `a title change without a host change is not a visit`() = runTest {
        val tabs = MutableStateFlow(listOf(tab("tab_1", host = "docs.taffy.test", title = "a")))
        val recorder = RecordingRepository()
        FrequentSitesTracker(tabs, recorder).start(backgroundScope)
        runCurrent()

        tabs.value = listOf(tab("tab_1", host = "docs.taffy.test", title = "b"))
        runCurrent()

        assertEquals(emptyList<String>(), recorder.visits)
    }

    private fun tab(
        id: String,
        host: String,
        title: String = host,
        isPrivate: Boolean = false,
        isTaffyTab: Boolean = false,
    ): Tab = Tab(
        id = TabId(id),
        title = title,
        host = host,
        isPrivate = isPrivate,
        isTaffyTab = isTaffyTab,
    )

    private class RecordingRepository : FrequentSitesRepository {
        val visits = mutableListOf<String>()

        override val sites: StateFlow<List<FrequentSite>> = MutableStateFlow(emptyList())

        override suspend fun recordVisit(host: String, title: String) {
            visits += host
        }
    }
}
