// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Every provider the published catalog serves draws something.
 *
 * A badge has two branches and no third: a vendored mark, or the monogram. The
 * failure this file exists for is the branch nobody can see — a provider id
 * that matches no `when` arm and whose display name is empty or whitespace
 * renders an empty chip on a row a person is being asked to choose, and
 * nothing anywhere fails. So the walk is over ids and names together, and both
 * branches are asserted to produce something.
 *
 * The sixteen rows are transcribed from the published catalog source,
 * `taffy-core/components/intelligence/core/rust/model-router/catalog/source/
 * providers.json`. They are a copy, and a copy can go stale — a seventeenth
 * provider added there is not added here by anything. What this file can still
 * do, and does, is fail the moment somebody removes a mark or renames an id
 * that these sixteen depend on, which is the drift that actually happened
 * before: the catalog gained ten providers and the badge kept answering for
 * six.
 */
class ProviderMarkResolutionTest {

    private val catalog = listOf(
        "anthropic" to "Anthropic",
        "baseten" to "Baseten",
        "cerebras" to "Cerebras",
        "deepseek" to "DeepSeek",
        "fireworks" to "Fireworks AI",
        "github-copilot" to "GitHub Copilot",
        "google-ai-studio" to "Google AI Studio",
        "groq" to "Groq",
        "kimi-coding" to "Kimi For Coding",
        "minimax" to "MiniMax",
        "moonshot" to "Moonshot AI",
        "openai" to "OpenAI",
        "openrouter" to "OpenRouter",
        "together" to "Together AI",
        "xai" to "xAI",
        "zai" to "Z.AI",
    )

    /** The catalog ids that draw a monogram on purpose, and why, in one place. */
    private val monogramOnPurpose = setOf("zai")

    @Test
    fun `every catalog provider resolves to a mark or to a monogram`() {
        assertEquals(16, catalog.size)
        catalog.forEach { (id, name) ->
            val mark = vendoredMark(id)
            val monogram = providerMonogram(name)
            assertTrue("$id draws nothing", mark != null || monogram.isNotBlank())
        }
    }

    @Test
    fun `the providers left on a monogram are exactly the ones argued for`() {
        val withoutMark = catalog.filter { (id, _) -> vendoredMark(id) == null }.map { it.first }

        assertEquals(monogramOnPurpose.toList().sorted(), withoutMark.sorted())
    }

    @Test
    fun `a provider left on a monogram still has a letter to draw`() {
        catalog.filter { (id, _) -> id in monogramOnPurpose }.forEach { (id, name) ->
            assertEquals(id, 1, providerMonogram(name).length)
        }
    }

    @Test
    fun `an id the catalog has never served falls through to the monogram`() {
        assertNull(vendoredMark("a-provider-added-after-this-build"))
        assertNull(ProviderMarkStyle.inkFor("a-provider-added-after-this-build"))
        assertEquals("A", providerMonogram("  acme models "))
    }

    @Test
    fun `every recorded hue that is drawn is legible on the chip it sits on`() {
        catalog.forEach { (id, _) ->
            val hue = ProviderMarkStyle.recordedHue(id) ?: return@forEach
            val drawn = ProviderMarkStyle.inkFor(id) != null

            assertEquals(id, ProviderMarkStyle.readsOnChip(hue), drawn)
            if (drawn) {
                assertTrue(id, ProviderMarkStyle.contrastAgainstChip(hue) >= 3.0)
            }
        }
    }

    @Test
    fun `a hue too pale for the chip is rejected rather than drawn white on white`() {
        // Not a vendor's value: the point is that the rule is a measurement and
        // not a list of the vendors that happen to be dark today.
        assertTrue(ProviderMarkStyle.readsOnChip(0x00_00_00))
        assertTrue(!ProviderMarkStyle.readsOnChip(0xFF_FF_F0))
        assertTrue(!ProviderMarkStyle.readsOnChip(0xFF_FF_FF))
    }

    /**
     * The two id-keyed tables `ProviderBadge` composes must agree.
     *
     * The badge reads `ProviderMarkStyle.inkFor` for its ground and
     * [vendoredMark] for what sits on it, and nothing but this assertion holds
     * the two together. An id in the hue table and not in the mark table draws
     * the monogram — `textSecondary`, which is `#C5C1BB` on the dark theme — on
     * the white chip the hue asked for, and `#C5C1BB` on white measures 1.79:1.
     * That is not a dim badge, it is an unreadable one, and it would look like
     * a rendering glitch rather than a missing table row.
     *
     * No id triggers it today, and the pair has drifted once already: until
     * 2026-08-30 a row since withdrawn was in one table and not the other. The
     * assertion is on the recorded hue rather than on the drawn ink, because a
     * hue recorded and rejected still means somebody has taught one table about
     * a provider the other has never heard of.
     */
    @Test
    fun `every id with a recorded hue has a mark to draw in it`() {
        catalog.forEach { (id, _) ->
            ProviderMarkStyle.recordedHue(id) ?: return@forEach

            assertNotNull(
                "$id has a recorded hue and no vendored mark: its badge would draw a " +
                    "monogram in textSecondary on the white chip, 1.79:1 on the dark theme",
                vendoredMark(id),
            )
        }
    }

    /**
     * There were two until decision 0214 took the Cloudflare row out of the
     * catalog. Its orange measured 2.65:1 and was recorded and rejected the same
     * way, and it left with the mark rather than staying behind as a hue for a
     * badge that can no longer be drawn.
     */
    @Test
    fun `the hue that measures short is recorded and not drawn`() {
        listOf("openrouter").forEach { id ->
            assertNotNull(id, ProviderMarkStyle.recordedHue(id))
            assertNull(id, ProviderMarkStyle.inkFor(id))
            assertNotNull(id, vendoredMark(id))
        }
    }

    /**
     * The provenance record said these two had no published colour, and the
     * distributor's own colour file said otherwise. Pinned so the mark cannot
     * quietly go back to neutral ink without the record going with it.
     */
    @Test
    fun `the two hues the colour files supplied are drawn`() {
        mapOf("cerebras" to 0xF1_5A_29L, "fireworks" to 0x50_19_C5L).forEach { (id, hue) ->
            val recorded = ProviderMarkStyle.recordedHue(id)
            assertNotNull(id, recorded)
            assertEquals(id, hue, recorded!!)
            assertNotNull(id, ProviderMarkStyle.inkFor(id))
            assertTrue(id, ProviderMarkStyle.contrastAgainstChip(hue) >= 3.0)
        }
    }

    /**
     * Together's colour is three fills and this table holds one value, so the
     * absence is the decision and not an oversight. A later `"together"` arm
     * would paint all three petals in one petal's fill — a re-drawing decision
     * 0030 forbids — and would otherwise pass every other test here, so it
     * fails this one instead.
     */
    @Test
    fun `Together keeps neutral ink because its recorded colour is a set`() {
        assertNull(ProviderMarkStyle.recordedHue("together"))
        assertNull(ProviderMarkStyle.inkFor("together"))
        assertNotNull(vendoredMark("together"))
    }

    /** Groq and Baseten have no colour file at the pin, so there is none to draw. */
    @Test
    fun `the two vendors with no published colour stay on neutral ink`() {
        listOf("groq", "baseten").forEach { id ->
            assertNull(id, ProviderMarkStyle.recordedHue(id))
            assertNotNull(id, vendoredMark(id))
        }
    }

}
