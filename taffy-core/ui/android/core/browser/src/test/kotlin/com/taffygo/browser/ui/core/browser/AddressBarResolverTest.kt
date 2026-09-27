// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.browser.internal.AddressBarResolver
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TaskTemplate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * One box, four readings (UX spec section 5), including the four the
 * specification's own table gives.
 */
class AddressBarResolverTest {

    @Test
    fun `the specification's four examples resolve the way it says they do`() {
        assertTrue(AddressBarResolver.resolve("openally.com") is AddressBarInterpretation.GoTo)
        assertTrue(AddressBarResolver.resolve("98 inch tv price") is AddressBarInterpretation.Search)
        assertTrue(
            AddressBarResolver.resolve("is this fee refundable?") is AddressBarInterpretation.AskTaffy,
        )
        assertTrue(
            AddressBarResolver.resolve("compare these 4 TV tabs") is AddressBarInterpretation.TaskForTaffy,
        )
    }

    @Test
    fun `a location keeps its host without the scheme`() {
        val resolved = AddressBarResolver.resolve("https://docs.example.test/policy")

        assertEquals("docs.example.test", (resolved as AddressBarInterpretation.GoTo).host)
    }

    @Test
    fun `web locations preserve safe host forms and scheme casing`() {
        val cases = mapOf(
            "HTTPS://Docs.Example.test:8443/policy?q=one#two" to "docs.example.test",
            "example.com:8443/admin" to "example.com",
            "127.0.0.1:8080/status" to "127.0.0.1",
            "[2001:db8::1]:8443/status" to "[2001:db8::1]",
            "http://localhost:8080/health" to "localhost",
        )

        for ((input, host) in cases) {
            val resolved = AddressBarResolver.resolve(input)
            assertTrue(input, resolved is AddressBarInterpretation.GoTo)
            assertEquals(input, host, (resolved as AddressBarInterpretation.GoTo).host)
            assertEquals(input, resolved.input)
        }
    }

    @Test
    fun `active external credentialed and ambiguous shapes never become navigation`() {
        val cases = listOf(
            "javascript:alert(document.domain)",
            "data:text/html,<script.src=example.com>",
            "file:///storage/emulated/0/private.txt",
            // The engine's own pages are Chromium's and not this product's:
            // TaffyGo draws its own settings, history and downloads. Typing one
            // is read as a search, so the address bar is the first of the three
            // places that keep it out (decision 0154).
            "chrome://settings",
            "chrome://flags",
            "CHROME://version",
            "chrome-untrusted://feed/",
            "devtools://devtools/bundled/inspector.html",
            "about:version",
            "about:blank",
            "content://provider.example/item/1",
            "view-source:https://example.com",
            "mailto:person@example.com",
            "custom://example.com/path",
            "https://person:secret@example.com/private",
            "person@example.com",
            "3.14",
            "999.999.999.999",
            "localhost:8080",
            "https://例え.テスト/",
        )

        for (input in cases) {
            assertFalse(input, AddressBarResolver.resolve(input) is AddressBarInterpretation.GoTo)
        }
    }

    @Test
    fun `only a task interpretation asks for a preview`() {
        val inputs = listOf("docs.example.test", "tv price", "why is this?", "compare these tabs")
        val needPreview = inputs.map(AddressBarResolver::resolve).map { it.needsPreview }

        assertEquals(listOf(false, false, false, true), needPreview)
    }

    @Test
    fun `each opener chooses the template the specification names`() {
        assertEquals(
            TaskTemplate.COMPARE_PRODUCTS,
            (AddressBarResolver.resolve("compare two policies") as AddressBarInterpretation.TaskForTaffy)
                .template,
        )
        assertEquals(
            TaskTemplate.SUMMARIZE_EVIDENCE,
            (AddressBarResolver.resolve("summarise the reviews") as AddressBarInterpretation.TaskForTaffy)
                .template,
        )
        assertEquals(
            TaskTemplate.BUILD_A_SOURCE_TABLE,
            (AddressBarResolver.resolve("collect the sources") as AddressBarInterpretation.TaskForTaffy)
                .template,
        )
    }

    @Test
    fun `common research table and errand requests reach reviewed task previews`() {
        val cases = listOf(
            "research a trip to Kochi" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "analyse the selected policy" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "build an itinerary for Kerala" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "transcribe this interview" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "extract text from this PDF" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "translate this page to Hindi" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "make a presentation from these sources" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "write a report about this policy" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "analyze this video for key moments" to TaskTemplate.SUMMARIZE_EVIDENCE,
            "which should I buy from these tabs" to TaskTemplate.COMPARE_PRODUCTS,
            "shortlist three laptops" to TaskTemplate.COMPARE_PRODUCTS,
            "organise these invoices" to TaskTemplate.BUILD_A_SOURCE_TABLE,
            "create a spreadsheet from these invoices" to TaskTemplate.BUILD_A_SOURCE_TABLE,
            "convert to CSV from this table" to TaskTemplate.BUILD_A_SOURCE_TABLE,
            "extract fields from these receipts" to TaskTemplate.BUILD_A_SOURCE_TABLE,
            "analyze this data for outliers" to TaskTemplate.BUILD_A_SOURCE_TABLE,
            "choose between these laptops" to TaskTemplate.COMPARE_PRODUCTS,
            "book a refundable hotel" to TaskTemplate.WEB_ERRAND,
            "fill out this application" to TaskTemplate.WEB_ERRAND,
            "prefill this registration" to TaskTemplate.WEB_ERRAND,
            "download the selected report" to TaskTemplate.WEB_ERRAND,
        )

        for ((input, expected) in cases) {
            val resolved = AddressBarResolver.resolve(input)
            assertTrue(input, resolved is AddressBarInterpretation.TaskForTaffy)
            assertEquals(input, expected, (resolved as AddressBarInterpretation.TaskForTaffy).template)
            assertTrue(input, resolved.needsPreview)
        }
    }

    @Test
    fun `explicit price comparisons choose the selected source comparison preview`() {
        for (input in listOf(
            "which is cheaper?",
            "Which is cheapest?",
            "which costs less?",
            "which of these is cheaper?",
            "which is cheaper between these phones",
        )) {
            assertEquals(
                input,
                AddressBarInterpretation.TaskForTaffy(input, TaskTemplate.COMPARE_PRODUCTS),
                AddressBarResolver.resolve(input),
            )
        }
    }

    @Test
    fun `explicit requests to find cheaper prices choose the errand preview`() {
        for (input in listOf(
            "where can I get this phone cheaper",
            "where can I buy this phone cheaper?",
            "where can I find the cheapest Pixel?",
            "find this phone cheaper",
            "find a cheaper phone",
            "find the cheapest Pixel",
            "  WHERE CAN I GET this phone CHEAPER?  ",
        )) {
            val expected = AddressBarInterpretation.TaskForTaffy(input.trim(), TaskTemplate.WEB_ERRAND)
            val resolved = AddressBarResolver.resolve(input)
            assertEquals(input, expected, resolved)
            assertTrue(input, resolved.needsPreview)
        }
    }

    @Test
    fun `price words alone explanatory questions and unrelated find requests stay ordinary readings`() {
        val questions = listOf(
            "why is this phone cheaper?",
            "where can I get this phone?",
            "where can I get a cheapestcase?",
            "which is cheaperphone?",
        )
        for (input in questions) {
            assertEquals(input, AddressBarInterpretation.AskTaffy(input), AddressBarResolver.resolve(input))
        }
        val searches = listOf(
            "cheaper",
            "cheaper phone prices",
            "find out why phones are cheaper",
            "find me a phone",
            "find cheaperphone deals",
        )
        for (input in searches) {
            assertEquals(input, AddressBarInterpretation.Search(input), AddressBarResolver.resolve(input))
        }
    }

    @Test
    fun `price words in web locations still navigate without a task preview`() {
        for (input in listOf(
            "https://shop.example.test/find/cheaper?which=cheapest",
            "cheaper.example.test/where-can-i-buy",
        )) {
            val resolved = AddressBarResolver.resolve(input)
            assertTrue(input, resolved is AddressBarInterpretation.GoTo)
            assertFalse(input, resolved.needsPreview)
        }
    }

    @Test
    fun `an exact task verb still reaches a preview`() {
        val resolved = AddressBarResolver.resolve("compare")

        assertEquals(
            TaskTemplate.COMPARE_PRODUCTS,
            (resolved as AddressBarInterpretation.TaskForTaffy).template,
        )
    }

    @Test
    fun `an empty box searches rather than starting anything`() {
        val resolved = AddressBarResolver.resolve("   ")

        assertTrue(resolved is AddressBarInterpretation.Search)
        assertFalse(resolved.needsPreview)
    }

    @Test
    fun `the alternatives never repeat the reading already chosen`() {
        for (input in listOf("docs.example.test", "tv price", "why is this?", "compare these tabs")) {
            val chosen = AddressBarResolver.resolve(input)
            val alternatives = AddressBarResolver.alternatives(input)

            assertTrue(input, alternatives.none { it::class == chosen::class })
        }
    }
}
