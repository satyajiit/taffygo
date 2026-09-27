// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A task's search asks the engine for English; a person's search does not.
 *
 * Decision 0172: a task's results page becomes the task's evidence and is read
 * by a model working in English, so a page served in the device's regional
 * language is evidence the answer cannot quote. A person's own search is their
 * search and is left alone.
 */
class SearchEngineTaskAddressTest {

    @Test
    fun `a task's google search asks for english and a person's does not`() {
        val query = "Download my eAadhaar"

        val person = SearchEngineCatalog.searchUrl(SearchEngineId.GOOGLE, query)
        val task = SearchEngineCatalog.taskSearchUrl(SearchEngineId.GOOGLE, query)

        assertEquals("https://www.google.com/search?q=Download+my+eAadhaar", person)
        assertEquals("$person&hl=en&lr=lang_en", task)
    }

    @Test
    fun `every offered engine answers a task, and none is guessed at`() {
        for (engine in SearchEngineCatalog.entries) {
            val address = SearchEngineCatalog.taskSearchUrl(engine.id, "aadhaar")
            assertTrue("${engine.id}", address != null && address.startsWith("https://"))
            val plain = SearchEngineCatalog.searchUrl(engine.id, "aadhaar")
            if (engine.englishQuery.isEmpty()) {
                assertEquals("${engine.id}", plain, address)
            } else {
                assertEquals("${engine.id}", "$plain&${engine.englishQuery}", address)
            }
        }
    }

    @Test
    fun `nothing to search is not an address`() {
        assertNull(SearchEngineCatalog.taskSearchUrl(SearchEngineId.GOOGLE, "   "))
    }

    @Test
    fun `the repository the task path reads answers the task address`() {
        val repository = MemorySearchEngineRepository(SearchEngineId.GOOGLE)

        assertEquals(
            SearchEngineCatalog.taskSearchUrl(SearchEngineId.GOOGLE, "aadhaar"),
            repository.taskSearchUrl("aadhaar"),
        )
    }
}
