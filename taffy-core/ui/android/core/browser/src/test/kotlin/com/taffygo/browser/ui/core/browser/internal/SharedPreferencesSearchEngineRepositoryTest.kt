// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.browser.SearchEngineId
import java.io.Closeable
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

internal class SharedPreferencesSearchEngineRepositoryTest {
    @Test
    fun `a choice reaches every live window and a closed window stops observing`() = runTest {
        val store = FakeSelectionStore()
        val first = SharedPreferencesSearchEngineRepository(store)
        val second = SharedPreferencesSearchEngineRepository(store)

        first.select(SearchEngineId.BING)
        assertEquals(SearchEngineId.BING, first.selectedId.value)
        assertEquals(SearchEngineId.BING, second.selectedId.value)

        second.select(SearchEngineId.QWANT)
        assertEquals(SearchEngineId.QWANT, first.selectedId.value)
        assertEquals(SearchEngineId.QWANT, second.selectedId.value)

        first.close()
        second.select(SearchEngineId.ECOSIA)
        assertEquals(SearchEngineId.QWANT, first.selectedId.value)
        assertEquals(SearchEngineId.ECOSIA, second.selectedId.value)
        assertEquals(1, store.listenerCount)
    }

    @Test
    fun `an external write is parsed fail closed by every live window`() {
        val store = FakeSelectionStore()
        val first = SharedPreferencesSearchEngineRepository(store)
        val second = SharedPreferencesSearchEngineRepository(store)

        store.externalWrite("not-an-engine")

        assertEquals(SearchEngineId.DEFAULT, first.selectedId.value)
        assertEquals(SearchEngineId.DEFAULT, second.selectedId.value)
    }

    private class FakeSelectionStore : SearchEngineSelectionStore {
        private var value = ""
        private val listeners = linkedSetOf<() -> Unit>()

        val listenerCount: Int
            get() = listeners.size

        override fun read(): String = value

        override fun write(wireName: String) = externalWrite(wireName)

        override fun observe(onChanged: () -> Unit): Closeable {
            listeners += onChanged
            return Closeable { listeners -= onChanged }
        }

        fun externalWrite(wireName: String) {
            value = wireName
            listeners.toList().forEach { it() }
        }
    }
}
