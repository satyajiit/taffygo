// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows
import java.util.Locale

/**
 * Screen SCR-417's pure half: three published facts and one local draft, read
 * as one list.
 *
 * Everything the screen can draw is decided here, so a host test can prove the
 * whole of it — which models are listed, which one is in force, which providers
 * a person cannot use yet and where pressing those leads, and whether the
 * thinking control exists at all — without a device, a provider or a network.
 *
 * Nothing here invents a rung, and nothing here reads a provider's identity.
 * The rungs offered are the model's own `thinkingLevels`, the standing choice
 * is the roster row's, the kind of work is `ModelCapability.of`, and whether a
 * provider can be used is the same intersection screen SCR-404 performs through
 * `ProviderRowDispatch` — so a provider a catalog update introduces is listed
 * correctly by a binary compiled before it existed.
 */
object ModelSelectionProjection {

    /**
     * Fold the published roster, the published catalog, the browser's handles
     * and the draft into the screen.
     *
     * A roster that is not ready yields [ModelSelectionUiState.Status.LOADING]
     * whatever it carries, for the reason screen SCR-404 states: `ready` is the
     * only thing that separates a core that has not spoken from a catalog with
     * nothing in it, and a screen that read an empty pre-publication list would
     * announce that this provider offers no models.
     */
    fun project(
        providerId: String?,
        roster: ProviderRosterState,
        models: Map<String, List<ProviderModel>>,
        browserHeldCredentialIds: Set<String>,
        draft: ModelSelectionDraft,
        signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
    ): ModelSelectionUiState {
        val blank = ModelSelectionUiState(providerId = providerId, query = draft.query)
        if (!roster.ready) return blank.copy(status = ModelSelectionUiState.Status.LOADING)

        val listed = roster.rows.filter { providerId == null || it.providerId == providerId }
        if (providerId != null && listed.isEmpty()) {
            return blank.copy(status = ModelSelectionUiState.Status.UNKNOWN)
        }

        val settled = ModelSelectionReducer.settle(draft, roster)
        val blocks = listed.mapNotNull { row ->
            blockFor(
                row = row,
                models = models[row.providerId].orEmpty(),
                browserHeldCredentialIds = browserHeldCredentialIds,
                draft = settled,
                signInFlows = signInFlows,
            )
        }
        if (blocks.isEmpty()) return blank.copy(status = ModelSelectionUiState.Status.EMPTY)

        val matched = blocks.mapNotNull { narrow(it, settled.query) }
        if (matched.isEmpty()) return blank.copy(status = ModelSelectionUiState.Status.NO_MATCH)

        val (ready, locked) = matched.partition { !it.locked }
        return blank.copy(
            status = ModelSelectionUiState.Status.READY,
            ready = ready,
            locked = locked,
            // Nothing to pick from above it would leave the page a heading over
            // an empty list, so the shut half opens itself rather than hiding
            // the only models there are.
            lockedExpanded = settled.lockedExpanded || ready.isEmpty(),
        )
    }

    /**
     * One provider's models, or null when the catalog named none for it.
     *
     * A provider the catalog carries nothing for is dropped rather than drawn
     * as an empty heading: the page is about models, and a provider that
     * contributes none is a fact for screen SCR-404 rather than a row here.
     * A provider the catalog carries models for and the published list holds
     * *none* of is the opposite case and is kept: it is a list cut to nothing
     * by the budget every provider shares, and hiding it would present the
     * page as whole when it is not (decision 0098 section 4). The block then
     * has no groups and says how many it is missing.
     */
    private fun blockFor(
        row: ProviderRosterRow,
        models: List<ProviderModel>,
        browserHeldCredentialIds: Set<String>,
        draft: ModelSelectionDraft,
        signInFlows: Map<String, Boolean>,
    ): ModelSelectionUiState.Block? {
        if (models.isEmpty() && row.modelCount == 0) return null
        val locked = !ProviderRowDispatch.configuredFor(
            row, ProviderRowDispatch.availabilityFor(row, browserHeldCredentialIds),
        )
        return ModelSelectionUiState.Block(
            providerId = row.providerId,
            providerName = row.displayName,
            locked = locked,
            offer = ProviderRowDispatch.offerFor(row, signInFlows),
            groups = grouped(models.map { rowFor(row, it, locked, draft) }),
            listedCount = models.size,
            catalogCount = row.modelCount,
        )
    }

    /**
     * Models under the name of the work they do.
     *
     * The kinds are listed in the enumeration's order, which is the order the
     * screen reads them in, and a kind no model does draws no heading.
     */
    private fun grouped(
        rows: List<ModelSelectionUiState.Row>,
    ): List<ModelSelectionUiState.Group> =
        ModelCapability.entries.mapNotNull { capability ->
            rows.filter { it.capability == capability }
                .takeIf { it.isNotEmpty() }
                ?.let { ModelSelectionUiState.Group(capability = capability, rows = it) }
        }

    private fun rowFor(
        provider: ProviderRosterRow,
        model: ProviderModel,
        locked: Boolean,
        draft: ModelSelectionDraft,
    ): ModelSelectionUiState.Row = ModelSelectionUiState.Row(
        providerId = provider.providerId,
        providerName = provider.displayName,
        modelId = model.modelId,
        displayName = model.displayName,
        capability = ModelCapability.of(model),
        // Both badges are read straight off the entry rather than off the
        // group: a model that reasons *and* reads pictures is filed once, under
        // the first, and would otherwise lose the second on its own row.
        reasoning = model.reasoning,
        readsPictures = ModelInputModality.IMAGE in model.inputModalities,
        contextWindow = model.contextWindow,
        // The roster's own pin, and only that. A provider with none has none:
        // its own order stands, and the page does not guess which model that is
        // by taking the first one in the list.
        selected = provider.selectedModelId == model.modelId,
        asking = draft.asked?.providerId == provider.providerId &&
            draft.asked.modelId == model.modelId,
        locked = locked,
        rungs = model.thinkingLevels,
        standing = provider.thinking,
    )

    /**
     * The block narrowed to what was typed, or null when nothing in it matches.
     *
     * A model is matched by its name, its identifier, and its provider's name
     * and identifier, because all four are things a person might have read
     * somewhere and come here to find. Matching is case-insensitive in the
     * root locale: this is comparing catalog data against typed text, not
     * sorting words for a reader, and a locale-sensitive fold would make the
     * same query match different models on two phones.
     */
    private fun narrow(
        block: ModelSelectionUiState.Block,
        query: String,
    ): ModelSelectionUiState.Block? {
        val needle = query.trim().lowercase(Locale.ROOT)
        if (needle.isEmpty()) return block
        val groups = block.groups.mapNotNull { group ->
            group.rows.filter { it.matches(needle) }
                .takeIf { it.isNotEmpty() }
                ?.let { group.copy(rows = it) }
        }
        // The two counts ride through untouched: a search hides rows, and the
        // budget's cut is a different fact from what was typed.
        return if (groups.isEmpty()) null else block.copy(groups = groups)
    }

    private fun ModelSelectionUiState.Row.matches(needle: String): Boolean =
        displayName.lowercase(Locale.ROOT).contains(needle) ||
            modelId.lowercase(Locale.ROOT).contains(needle) ||
            providerName.lowercase(Locale.ROOT).contains(needle) ||
            providerId.lowercase(Locale.ROOT).contains(needle)
}
