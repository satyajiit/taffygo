// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel

/**
 * Screen SCR-417 — the models on offer, for one provider or across the whole
 * catalog.
 *
 * The screen holds no opinion of its own about what stands. [Row.selected] is
 * read from the roster the isolated core last published, so what is drawn as in
 * use is the choice the core accepted rather than the one this screen asked
 * for: a preference the core refuses simply never appears, where a locally
 * optimistic copy would show it as though it had taken. [Row.asking] is the
 * separate, weaker fact that a command went out and has not been answered.
 *
 * The list is two levels deep and the levels answer different questions. A
 * [Block] answers *whose* — one provider, and whether this browser can reach it
 * at all. A [Group] answers *what kind of work* — read from the model's own
 * catalog entry, never from its name.
 */
data class ModelSelectionUiState(
    /** Whether there is a list, and what to say when there is not. */
    val status: Status = Status.LOADING,
    /** The provider this list is narrowed to, or null for the whole catalog. */
    val providerId: String? = null,
    /** What has been typed into the search field. */
    val query: String = "",
    /** Providers already configured, including keyless own endpoints, in the core's order. */
    val ready: List<Block> = emptyList(),
    /** Providers nothing stands behind yet, in the core's order. */
    val locked: List<Block> = emptyList(),
    /** Whether the providers that still need setting up are showing their models. */
    val lockedExpanded: Boolean = false,
) {
    /** Every row on the page, for a reader that wants the flat list. */
    val rows: List<Row> by lazy(LazyThreadSafetyMode.NONE) {
        buildList {
            for (block in ready) addAll(block.rows)
            for (block in locked) addAll(block.rows)
        }
    }

    /** Whether the search field has narrowed the list. */
    val searching: Boolean get() = query.isNotBlank()

    /**
     * Whether the search field is worth drawing.
     *
     * A field over a list that has nothing in it is a control that can only
     * make the page emptier, so it appears once there is something to narrow —
     * and stays while a query is standing, or clearing it would be impossible.
     */
    val searchable: Boolean get() = searching || status == Status.READY

    /**
     * One provider, and the models it carries.
     *
     * [locked] is the whole difference between the two halves of the page: a
     * locked block's rows lead to the provider's own page instead of pinning a
     * model, because pinning a model on a provider this browser cannot reach
     * would be a choice with nothing behind it.
     */
    data class Block(
        /** Stable identity, and the key a choice is filed under. */
        val providerId: String,
        /** That provider's name, as the catalog spells it. */
        val providerName: String,
        /** Whether this provider still needs setting up before its models can be used. */
        val locked: Boolean,
        /**
         * What this provider offers when a locked row is pressed, which is the
         * same answer screen SCR-404 gives for the same row. Read only while
         * [locked]: a provider already reachable is not being set up here.
         */
        val offer: ProviderRowOffer,
        /** The non-empty groups, in the order the kinds of work are listed. */
        val groups: List<Group>,
        /**
         * How many of this provider's models the core published, before any
         * search narrowed them. Counted where the list was whole, so a query
         * that hides rows does not read as the budget having taken them.
         */
        val listedCount: Int,
        /**
         * How many models the merged catalog carries for this provider, as
         * the core counted them before the flat list was fitted to the budget
         * every provider shares (decision 0098 section 4).
         */
        val catalogCount: Int,
    ) {
        /** Every model in this block. */
        val rows: List<Row> by lazy(LazyThreadSafetyMode.NONE) {
            buildList {
                for (group in groups) addAll(group.rows)
            }
        }

        /** Whether pressing a locked row leads anywhere. */
        val pressable: Boolean get() = offer !is ProviderRowOffer.Blocked

        /**
         * Whether the shared budget cut this provider's list.
         *
         * The difference between the two counts is exactly the rows this
         * provider lost, and a list presented as whole when it is not is the
         * failure decision 0098 section 4 names — so the page says so rather
         * than drawing the survivors as though they were everything.
         */
        val truncated: Boolean get() = catalogCount > listedCount
    }

    /** Models that do the same kind of work, under the name of that work. */
    data class Group(
        val capability: ModelCapability,
        val rows: List<Row>,
    )

    /** One model, and — where it is the one in force — the thinking beside it. */
    data class Row(
        /** The provider that carries this model, and the key a choice is filed under. */
        val providerId: String,
        /** That provider's name, as the catalog spells it. */
        val providerName: String,
        /** The model's identity, and what a preference names. */
        val modelId: String,
        /** The name to show, never the identifier. */
        val displayName: String,
        /** Which kind of work this model is filed under. */
        val capability: ModelCapability,
        /** Whether it reasons before it answers. */
        val reasoning: Boolean,
        /** Whether it accepts pictures as well as text. */
        val readsPictures: Boolean,
        /** How much it can be given at once, zero when the catalog said nothing. */
        val contextWindow: ULong,
        /** Whether this is the model in force for its provider, as the roster says. */
        val selected: Boolean,
        /** Whether a command naming this model went out and has not been answered. */
        val asking: Boolean,
        /** Whether its provider still needs setting up. */
        val locked: Boolean,
        /** Exactly the rungs this model offers, ascending. */
        val rungs: List<ThinkingLevel> = emptyList(),
        /** The rung standing for this provider, null while Taffy decides. */
        val standing: ThinkingLevel? = null,
    ) {
        /**
         * The thinking control for this row, drawn only where the model is the
         * one in force: a rung asked of a model nobody chose is not a standing
         * state, and offering one would suggest it were.
         */
        val thinking: ThinkingChoice
            get() = ThinkingChoice(rungs = rungs, chosen = if (selected) standing else null)

        /**
         * The rung that should stand once this model is pinned.
         *
         * The command states the whole choice, so choosing a model has to name
         * a thinking level too. A rung this person already asked for survives
         * when the new model offers it and drops to Auto when it does not —
         * never carried across as an amount the model cannot do, and never left
         * unstated, which would silently clear it either way.
         */
        val thinkingAfterChoosing: ThinkingLevel?
            get() = standing?.takeIf { it in rungs }
    }

    /**
     * What the screen has to say when it has no models to draw.
     *
     * The three absences are three different sentences on purpose. Waiting,
     * a catalog that named nothing, and a search that matched nothing each have
     * a different cause and a different next thing to do, and one shared "no
     * models" would send somebody looking for a fault in the two cases where
     * there is none.
     *
     * [UNKNOWN] is not an error either: the catalog is served (decision 0080),
     * so a provider can leave it between one snapshot and the next, and a
     * screen opened from a stale back stack is then about something the browser
     * no longer carries.
     */
    enum class Status {
        /** The core has not published a roster yet. */
        LOADING,

        /** The roster arrived and does not carry the named provider. */
        UNKNOWN,

        /** The roster arrived and names no model that can be offered here. */
        EMPTY,

        /** There are models, and what was typed excludes all of them. */
        NO_MATCH,

        /** There are models to draw. */
        READY,
    }
}
