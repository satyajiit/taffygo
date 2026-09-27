// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.ThinkingLevel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-417's list, its absences, and the thinking control beside the
 * model in force.
 *
 * The thinking half is what this suite is for: the rungs offered are the
 * model's own, a model offering fewer than two draws no control at all, and
 * **Auto** is the absence of a rung rather than [ThinkingLevel.OFF].
 */
class ModelSelectionProjectionTest {

    private val ladder = listOf(ThinkingLevel.OFF, ThinkingLevel.LOW, ThinkingLevel.HIGH)
    private val connected = setOf("alpha", "beta")

    private fun project(
        providerId: String?,
        roster: ProviderRosterState,
        models: Map<String, List<com.taffygo.browser.ui.core.model.ProviderModel>>,
        held: Set<String> = connected,
        draft: ModelSelectionDraft = ModelSelectionDraft(),
    ) = ModelSelectionProjection.project(
        providerId = providerId,
        roster = roster,
        models = models,
        browserHeldCredentialIds = held,
        draft = draft,
    )

    @Test
    fun `nothing is listed until the core has published a roster`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(ready = false, rows = listOf(providerRow("alpha"))),
            models = mapOf("alpha" to listOf(providerModel("alpha", "one"))),
        )

        assertEquals(ModelSelectionUiState.Status.LOADING, state.status)
        assertTrue(state.rows.isEmpty())
    }

    @Test
    fun `a provider the roster no longer carries is said, not waited for`() {
        val state = project(
            providerId = "gone",
            roster = ProviderRosterState(ready = true, rows = listOf(providerRow("alpha"))),
            models = emptyMap(),
        )

        assertEquals(ModelSelectionUiState.Status.UNKNOWN, state.status)
    }

    @Test
    fun `a published roster naming no model is empty, not loading`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(ready = true, rows = listOf(providerRow("alpha"))),
            models = emptyMap(),
        )

        assertEquals(ModelSelectionUiState.Status.EMPTY, state.status)
    }

    @Test
    fun `the rung drawn is the roster's echo, and Auto is its absence`() {
        fun screen(standing: ThinkingLevel?) = project(
            providerId = "alpha",
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(
                    providerRow("alpha", selectedModelId = "one", thinking = standing),
                ),
            ),
            models = mapOf(
                "alpha" to listOf(providerModel("alpha", "one", thinkingLevels = ladder)),
            ),
        )

        val auto = screen(null).rows.single().thinking
        val off = screen(ThinkingLevel.OFF).rows.single().thinking

        // Two different published facts, two different standing answers. The
        // roster is the only source: nothing here remembers what was pressed.
        assertTrue(auto.automatic)
        assertNull(auto.chosen)
        assertTrue(auto.stands(null))
        assertFalse(auto.stands(ThinkingLevel.OFF))

        assertFalse(off.automatic)
        assertEquals(ThinkingLevel.OFF, off.chosen)
        assertFalse(off.stands(null))
    }

    @Test
    fun `a rung is only offered on the model that is in force`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(
                    providerRow("alpha", selectedModelId = "one", thinking = ThinkingLevel.HIGH),
                ),
            ),
            models = mapOf(
                "alpha" to listOf(
                    providerModel("alpha", "one", thinkingLevels = ladder),
                    providerModel("alpha", "two", thinkingLevels = ladder),
                ),
            ),
        )

        assertEquals(
            ThinkingLevel.HIGH,
            state.rows.single { it.modelId == "one" }.thinking.chosen,
        )
        // A rung asked of a model nobody chose is not a standing state.
        assertNull(state.rows.single { it.modelId == "two" }.thinking.chosen)
    }

    @Test
    fun `a model with fewer than two rungs offers no control`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(ready = true, rows = listOf(providerRow("alpha"))),
            models = mapOf(
                "alpha" to listOf(
                    providerModel("alpha", "fixed", thinkingLevels = listOf(ThinkingLevel.MEDIUM)),
                    providerModel("alpha", "none"),
                ),
            ),
        )

        assertTrue(state.rows.none { it.thinking.offered })
    }

    @Test
    fun `the rungs offered are the model's own, never the enumeration's`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(ready = true, rows = listOf(providerRow("alpha"))),
            models = mapOf(
                "alpha" to listOf(providerModel("alpha", "one", thinkingLevels = ladder)),
            ),
        )

        val offered = state.rows.single().thinking
        assertTrue(offered.offered)
        assertEquals(ladder, offered.rungs)
        // XHIGH and MAX are opt-in: a model whose catalog entry does not map
        // them must not be able to be asked for them.
        assertFalse(ThinkingLevel.XHIGH in offered.rungs)
        assertFalse(ThinkingLevel.MAX in offered.rungs)
    }

    @Test
    fun `a rung survives a change of model only where the new model offers it`() {
        val state = project(
            providerId = "alpha",
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(
                    providerRow("alpha", selectedModelId = "wide", thinking = ThinkingLevel.HIGH),
                ),
            ),
            models = mapOf(
                "alpha" to listOf(
                    providerModel("alpha", "wide", thinkingLevels = ladder),
                    providerModel(
                        "alpha",
                        "narrow",
                        thinkingLevels = listOf(ThinkingLevel.OFF, ThinkingLevel.LOW),
                    ),
                ),
            ),
        )

        assertEquals(
            ThinkingLevel.HIGH,
            state.rows.single { it.modelId == "wide" }.thinkingAfterChoosing,
        )
        // The narrow model cannot do HIGH, so the choice drops to Auto rather
        // than being carried across as an amount that model does not offer.
        assertNull(state.rows.single { it.modelId == "narrow" }.thinkingAfterChoosing)
    }

    @Test
    fun `the whole catalog names each model's provider and one row per provider stands`() {
        val state = project(
            providerId = null,
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(
                    providerRow("alpha", displayName = "Alpha", selectedModelId = "one"),
                    providerRow("beta", displayName = "Beta"),
                ),
            ),
            models = mapOf(
                "alpha" to listOf(providerModel("alpha", "one")),
                "beta" to listOf(providerModel("beta", "two")),
            ),
        )

        assertEquals(listOf("Alpha", "Beta"), state.rows.map { it.providerName })
        assertEquals(listOf(true, false), state.rows.map { it.selected })
    }

    // Decision 0098 section 4: the list is what survived the budget every
    // provider shares and the count is what the catalog carries. Both ride
    // on the block so the page can say what it is missing and whose.
    @Test
    fun `a block carries what was listed and what there is, and is cut only when they differ`() {
        val whole = project(
            providerId = "alpha",
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(providerRow("alpha", modelCount = 2)),
            ),
            models = mapOf(
                "alpha" to listOf(providerModel("alpha", "one"), providerModel("alpha", "two")),
            ),
        ).ready.single()
        val cut = project(
            providerId = "alpha",
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(providerRow("alpha", modelCount = 34)),
            ),
            models = mapOf("alpha" to listOf(providerModel("alpha", "one"))),
        ).ready.single()

        assertEquals(2, whole.listedCount)
        assertEquals(2, whole.catalogCount)
        assertFalse(whole.truncated)
        assertEquals(1, cut.listedCount)
        assertEquals(34, cut.catalogCount)
        assertTrue(cut.truncated)
    }

    // A search hides rows; the budget's cut is a different fact. Narrowing
    // must not make a whole list read as cut, nor a cut list read as whole.
    @Test
    fun `a search narrows the rows and leaves both counts alone`() {
        val roster = ProviderRosterState(
            ready = true,
            rows = listOf(providerRow("alpha", modelCount = 34)),
        )
        val models = mapOf(
            "alpha" to listOf(providerModel("alpha", "one"), providerModel("alpha", "two")),
        )

        val narrowed = project(
            providerId = "alpha",
            roster = roster,
            models = models,
            draft = ModelSelectionDraft(query = "two"),
        ).ready.single()

        assertEquals(listOf("two"), narrowed.rows.map { it.modelId })
        assertEquals(2, narrowed.listedCount)
        assertEquals(34, narrowed.catalogCount)
        assertTrue(narrowed.truncated)
    }

    // The 0-of-N case is the one decision 0098 section 4 forbids hiding: the
    // catalog carries models for this provider and the budget left it none.
    // A provider the catalog carries nothing for is still dropped, because
    // nothing was taken from it.
    @Test
    fun `a provider cut to nothing is kept with no groups, and one with nothing is dropped`() {
        val state = project(
            providerId = null,
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(
                    providerRow("emptied", modelCount = 12),
                    providerRow("nothing", modelCount = 0),
                    providerRow("alpha", modelCount = 1),
                ),
            ),
            models = mapOf("alpha" to listOf(providerModel("alpha", "one"))),
            held = setOf("emptied", "alpha"),
        )

        val emptied = state.ready.single { it.providerId == "emptied" }
        assertTrue(emptied.groups.isEmpty())
        assertEquals(0, emptied.listedCount)
        assertEquals(12, emptied.catalogCount)
        assertTrue(emptied.truncated)
        assertTrue(state.ready.none { it.providerId == "nothing" })
        assertTrue(state.locked.none { it.providerId == "nothing" })
    }

    @Test
    fun `a provider with no credential behind it cannot have a model pinned`() {
        val state = project(
            providerId = null,
            roster = ProviderRosterState(
                ready = true,
                rows = listOf(providerRow("alpha"), providerRow("nothing")),
            ),
            models = mapOf(
                "alpha" to listOf(providerModel("alpha", "one", thinkingLevels = ladder)),
                "nothing" to listOf(providerModel("nothing", "two", thinkingLevels = ladder)),
            ),
            held = setOf("alpha"),
        )

        assertEquals(listOf("alpha"), state.ready.map { it.providerId })
        assertEquals(listOf("nothing"), state.locked.map { it.providerId })
    }
}
