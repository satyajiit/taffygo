// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SearchEngineCatalogTest {

    @Test
    fun `google is the default and first in the list`() {
        assertEquals(SearchEngineId.GOOGLE, SearchEngineId.DEFAULT)
        assertEquals(SearchEngineId.GOOGLE, SearchEngineCatalog.entries.first().id)
    }

    @Test
    fun `yahoo japan is listed only for Japan`() {
        val us = SearchEngineCatalog.listed("US", SearchEngineId.GOOGLE).map { it.id }
        val jp = SearchEngineCatalog.listed("JP", SearchEngineId.GOOGLE).map { it.id }

        assertFalse(us.contains(SearchEngineId.YAHOO_JP))
        assertTrue(jp.contains(SearchEngineId.YAHOO_JP))
        assertTrue(us.contains(SearchEngineId.YAHOO))
        assertTrue(jp.contains(SearchEngineId.YAHOO))
    }

    @Test
    fun `a selected regional engine stays listed outside its country`() {
        val listed = SearchEngineCatalog.listed("US", SearchEngineId.YAHOO_JP).map { it.id }
        assertTrue(listed.contains(SearchEngineId.YAHOO_JP))
    }

    @Test
    fun `every id has one catalog row and tor is out`() {
        assertEquals(
            SearchEngineId.entries.toSet(),
            SearchEngineCatalog.entries.map { it.id }.toSet(),
        )
        assertTrue(
            SearchEngineCatalog.entries.none { engine ->
                engine.searchUrlTemplate.contains(".onion")
            },
        )
    }

    @Test
    fun `regional duckduckgo replaces the generic row`() {
        val us = SearchEngineCatalog.listed("US", SearchEngineId.GOOGLE).map { it.id }
        val de = SearchEngineCatalog.listed("DE", SearchEngineId.GOOGLE).map { it.id }
        val au = SearchEngineCatalog.listed("AU", SearchEngineId.GOOGLE).map { it.id }
        val nz = SearchEngineCatalog.listed("nz", SearchEngineId.GOOGLE).map { it.id }
        val ie = SearchEngineCatalog.listed("IE", SearchEngineId.GOOGLE).map { it.id }
        val jp = SearchEngineCatalog.listed("JP", SearchEngineId.GOOGLE).map { it.id }

        assertTrue(us.contains(SearchEngineId.DUCKDUCKGO))
        assertFalse(us.contains(SearchEngineId.DUCKDUCKGO_DE))
        assertFalse(us.contains(SearchEngineId.DUCKDUCKGO_AU_NZ_IE))

        assertTrue(de.contains(SearchEngineId.DUCKDUCKGO_DE))
        assertFalse(de.contains(SearchEngineId.DUCKDUCKGO))
        assertFalse(de.contains(SearchEngineId.DUCKDUCKGO_AU_NZ_IE))

        for (listed in listOf(au, nz, ie)) {
            assertTrue(listed.contains(SearchEngineId.DUCKDUCKGO_AU_NZ_IE))
            assertFalse(listed.contains(SearchEngineId.DUCKDUCKGO))
            assertFalse(listed.contains(SearchEngineId.DUCKDUCKGO_DE))
        }

        assertTrue(jp.contains(SearchEngineId.DUCKDUCKGO))
        assertFalse(jp.contains(SearchEngineId.DUCKDUCKGO_DE))
    }

    @Test
    fun `a selected regional duckduckgo stays listed outside its country`() {
        val listed = SearchEngineCatalog.listed("US", SearchEngineId.DUCKDUCKGO_DE).map { it.id }
        assertTrue(listed.contains(SearchEngineId.DUCKDUCKGO_DE))
        assertTrue(listed.contains(SearchEngineId.DUCKDUCKGO))
    }

    @Test
    fun `query addresses do not carry brave affiliate tags`() {
        for (engine in SearchEngineCatalog.entries) {
            val url = engine.searchUrlTemplate
            assertFalse(url, url.contains("t=brave"))
            assertFalse(url, url.contains("brz-brave"))
            assertFalse(url, url.contains("startpage.brave"))
            assertFalse(url, url.contains("addon=brave"))
            assertFalse(url, url.contains("clid="))
            assertFalse(url, url.contains("fr=brave"))
        }
    }

    @Test
    fun `a blank query is not an address`() {
        assertNull(SearchEngineCatalog.searchUrl(SearchEngineId.GOOGLE, "  "))
    }

    @Test
    fun `the query is placed on the engine's address`() {
        val url = SearchEngineCatalog.searchUrl(SearchEngineId.BING, "toffee recipe")
        assertTrue(url, url!!.startsWith("https://www.bing.com/search?q="))
        assertTrue(url, url.contains("toffee"))
        assertTrue(url, url.contains("recipe"))
    }

    @Test
    fun `regional duckduckgo uses the same duckduckgo address`() {
        val generic = SearchEngineCatalog.searchUrl(SearchEngineId.DUCKDUCKGO, "toffee")
        val germany = SearchEngineCatalog.searchUrl(SearchEngineId.DUCKDUCKGO_DE, "toffee")
        val auNzIe = SearchEngineCatalog.searchUrl(SearchEngineId.DUCKDUCKGO_AU_NZ_IE, "toffee")
        assertNotNull(generic)
        assertEquals(generic, germany)
        assertEquals(generic, auNzIe)
        assertTrue(generic, generic!!.startsWith("https://duckduckgo.com/?q="))
    }

    @Test
    fun `an unknown stored name becomes google`() {
        assertEquals(SearchEngineId.GOOGLE, SearchEngineId.parse(""))
        assertEquals(SearchEngineId.GOOGLE, SearchEngineId.parse("unknown"))
        assertEquals(SearchEngineId.ECOSIA, SearchEngineId.parse("ecosia"))
        assertEquals(SearchEngineId.DUCKDUCKGO_DE, SearchEngineId.parse("duckduckgo-de"))
        assertEquals(
            SearchEngineId.DUCKDUCKGO_AU_NZ_IE,
            SearchEngineId.parse("duckduckgo-au-nz-ie"),
        )
    }
}
