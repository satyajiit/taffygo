// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterState
import taffy.core_api.MAX_PROVIDER_ID_BYTES

/**
 * Screen SCR-418's pure half: the roster, one draft, and the address rules,
 * read as one page.
 *
 * Everything the page can draw is decided here — whether the address is
 * admitted, whether it is cleartext to a local machine, what the probe found,
 * whether a corrected address is being proposed, and whether the write can be
 * made — so a host test can prove the whole of it without a core, a server or
 * a network.
 */
object CustomEndpointProjection {

    /**
     * Fold the published roster and the draft into the page.
     *
     * A roster that is not ready yields [CustomEndpointUiState.Status.LOADING]
     * whatever it carries, for the reason screens SCR-404 and SCR-415 both
     * give: `ready` is the only thing separating a core that has not spoken
     * from a catalog with nothing in it, and a page that read an empty
     * pre-publication list would tell somebody their provider is gone.
     *
     * [currentHost] is the host the core published for the provider being
     * edited, and [currentAddress] the whole base it registered. The roster
     * carries both from Core API 3.18, which is what lets the field start at
     * what a person typed instead of empty: until then this page could name
     * where a provider pointed and had to ask them to type it again.
     *
     * [credentialHeld] is the browser's own answer about its secure store, and
     * the one thing on this page an address cannot supply. A key is never
     * filled back in — nothing can show it again — so the page has to say that
     * one is there and that an empty field keeps it.
     */
    fun project(
        endpointId: String?,
        roster: ProviderRosterState,
        models: Map<String, List<ProviderModel>>,
        draft: CustomEndpointDraft,
        currentHost: String? = null,
        currentAddress: String? = null,
        credentialHeld: Boolean = false,
    ): CustomEndpointUiState {
        if (!roster.ready) {
            return CustomEndpointUiState(
                status = CustomEndpointUiState.Status.LOADING,
                endpointId = endpointId,
            )
        }
        val row = endpointId?.let { id -> roster.rows.firstOrNull { it.providerId == id } }
        if (endpointId != null && row == null) {
            return CustomEndpointUiState(
                status = CustomEndpointUiState.Status.UNKNOWN,
                endpointId = endpointId,
            )
        }
        // The address a person is editing wins over the one the core
        // registered, and an untouched edit shows the registered one: a field
        // that reverted what somebody typed on the next published snapshot
        // would be the roster editing their draft.
        val shown = if (draft.addressTouched) draft.address else currentAddress ?: draft.address
        val typed = shown.trim()
        return CustomEndpointUiState(
            status = CustomEndpointUiState.Status.READY,
            endpointId = endpointId,
            address = shown,
            // The name follows the same rule for the same reason.
            name = if (draft.nameTouched || row == null) draft.name else row.displayName,
            // The key has no published half to fall back to and never will:
            // what is stored is sealed, and a field that showed dots standing
            // for a key nobody can read would be a control that lies about
            // what pressing save does.
            key = draft.key,
            keyRevealed = draft.keyRevealed,
            advancedOpen = draft.advancedOpen,
            keyStoreRefused = draft.keySealRefused,
            credentialHeld = credentialHeld,
            refusal = if (typed.isEmpty()) null else CustomEndpointAddress.classify(typed),
            cleartext = CustomEndpointAddress.isCleartextToLocal(typed),
            probing = draft.probing,
            outcome = draft.outcome,
            proposal = draft.proposal,
            keptAsTyped = draft.keptAsTyped,
            saving = draft.saving,
            saveRefused = draft.saveRefused,
            currentHost = currentHost,
            // The catalog's own name for the pinned model, never the
            // identifier: a model id is a wire value and reads like one. A pin
            // the roster no longer carries shows as no pin, which is what it
            // now is.
            pinnedModelName = row?.selectedModelId?.let { pinned ->
                models[row.providerId]?.firstOrNull { it.modelId == pinned }?.displayName
            },
            confirmingDelete = draft.confirmingDelete,
            deleting = draft.deleting,
        )
    }

    /**
     * The corrected address a probe answer names, or null.
     *
     * One line, and it is here rather than inlined in the view model so a host
     * test can hold the whole rule — probe answer in, proposal out — without
     * driving a screen.
     *
     * It reads the probe's own `provedBase` rather than working one out. That
     * is the difference Core API 3.18 makes: before it, this had to infer the
     * base from the typed address and the runtime the prober named, which is a
     * guess about somebody else's server. The prober knows which of its steps
     * answered and says so, and a guess and a demonstration are not
     * interchangeable on a screen whose whole subject is which address is real.
     *
     * Null in three cases, all of them "nothing to propose": the probe reached
     * nothing, the base it proved is the address as typed, or it proved no base
     * at all — and that last one is said by proposing nothing rather than by
     * proposing a value nobody demonstrated.
     */
    fun proposalFor(typed: String, outcome: CustomEndpointOutcome?): String? {
        val reached = outcome as? CustomEndpointOutcome.Reached ?: return null
        val proved = reached.provedBase ?: return null
        return proved.takeIf { it.trimEnd('/') != typed.trim().trimEnd('/') }
    }

    /**
     * An identity for a provider a person just named.
     *
     * `[a-z0-9][a-z0-9-]{0,63}` is what the browser's provider store files a
     * record under, and the first byte may not be the separator so an identity
     * can never be confused with a prefix. The name is folded into that
     * alphabet rather than asked for separately: a person naming their laptop
     * should not also have to invent a machine-readable identity for it.
     *
     * [taken] is every identity the roster already carries. A collision is
     * numbered rather than refused, because two servers called the same thing
     * is an ordinary situation and a screen that made somebody rename one
     * would be enforcing a rule that is not there.
     */
    fun mintProviderId(name: String, taken: Set<String>): String {
        val folded = name.lowercase()
            .map { if (it in 'a'..'z' || it in '0'..'9') it else '-' }
            .joinToString("")
            .split('-')
            .filter { it.isNotEmpty() }
            .joinToString("-")
            .take(MAX_PROVIDER_ID_BYTES)
            .trim('-')
        val base = folded.ifEmpty { FALLBACK_ID }
        if (base !in taken) return base
        var ordinal = 2
        while (numbered(base, ordinal) in taken) ordinal++
        return numbered(base, ordinal)
    }

    /** [base] with an ordinal, still inside the identity's bound. */
    private fun numbered(base: String, ordinal: Int): String {
        val suffix = "-$ordinal"
        return base.take(MAX_PROVIDER_ID_BYTES - suffix.length).trimEnd('-') + suffix
    }

    /** What a name made only of separators is filed as. */
    private const val FALLBACK_ID = "endpoint"
}
