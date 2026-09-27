// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFlows

/**
 * Screen SCR-415's pure half: five published facts and one local draft, read as
 * one page.
 *
 * Everything the page can draw is decided here, so a host test can prove the
 * whole of it — which way in is offered, whether the key form is even there,
 * what the button says, what the card over a stored credential shows, and
 * whether "Taffy uses this" can honestly be offered — without a device, a
 * provider or a network.
 *
 * No rule below reads a provider's identity except the one membership test
 * `ProviderRowDispatch` already justifies: whether this binary compiles the
 * vendor's sign-in flow, which is a fact about the build rather than about the
 * provider (decision 0081).
 */
object ProviderConfigProjection {

    /**
     * Fold the published facts and the draft into the page.
     *
     * A roster that is not ready yields [ProviderConfigUiState.Status.LOADING]
     * whatever it carries, for the reason screen SCR-404 states: `ready` is the
     * only thing that separates a core that has not spoken from a catalog with
     * nothing in it, and a page that read an empty pre-publication list would
     * announce that the provider is gone.
     */
    fun project(
        providerId: String,
        roster: ProviderRosterState,
        models: Map<String, List<ProviderModel>>,
        browserHeldCredentialIds: Set<String>,
        draft: ProviderConfigDraft,
        signInFlows: Map<String, Boolean> = ProviderSignInFlows.byVendor,
    ): ProviderConfigUiState {
        if (!roster.ready) {
            return ProviderConfigUiState(
                status = ProviderConfigUiState.Status.LOADING,
                providerId = providerId,
            )
        }
        val row = roster.rows.firstOrNull { it.providerId == providerId }
            ?: return ProviderConfigUiState(
                status = ProviderConfigUiState.Status.UNKNOWN,
                providerId = providerId,
            )

        val availability = ProviderRowDispatch.availabilityFor(row, browserHeldCredentialIds)
        val refusal = refusalFor(row)
        // Whether this build can actually put the person through the vendor's
        // sign-in — a carried flow the browser refuses to start is not a way
        // in, so it answers false here exactly as a missing flow does.
        val compiled = signInFlows[row.providerId] == true
        return ProviderConfigUiState(
            status = ProviderConfigUiState.Status.READY,
            providerId = row.providerId,
            displayName = row.displayName,
            blocked = refusal,
            signIn = if (refusal != null) {
                ProviderSignInOffer.NONE
            } else {
                signInOffer(row, compiled)
            },
            keyForm = if (refusal != null || RosterAuthMethod.API_KEY !in row.authMethods) {
                null
            } else {
                keyForm(row, draft)
            },
            managed = managedCredential(row, models, compiled, availability),
            presentation = row.presentation,
            defaultChoice = defaultChoice(
                row = row,
                availability = availability,
                rows = roster.rows,
                browserHeldCredentialIds = browserHeldCredentialIds,
            ),
            confirmingSignOut = draft.confirmingSignOut,
            signingOut = draft.signingOut,
        )
    }

    /**
     * Why nothing on this page can be acted on, or null.
     *
     * The same three roster refusals `ProviderRowDispatch.offerFor` states, in
     * the same order and for the same reason: each is something the product has
     * already declined, and the page must not open a form that is about to
     * refuse. A catalog entry naming no method at all joins them — there is
     * nothing to draw a control for, and saying so is better than an empty page.
     */
    private fun refusalFor(row: ProviderRosterRow): ProviderRowOffer.Reason? = when {
        !row.configurable -> ProviderRowOffer.Reason.NOT_ACTIONABLE
        !row.enabled -> ProviderRowOffer.Reason.HELD_SHUT
        row.authMethods.isEmpty() -> ProviderRowOffer.Reason.NO_METHOD
        else -> null
    }

    private fun signInOffer(row: ProviderRosterRow, compiled: Boolean): ProviderSignInOffer = when {
        RosterAuthMethod.OAUTH !in row.authMethods -> ProviderSignInOffer.NONE
        compiled -> ProviderSignInOffer.OFFERED
        else -> ProviderSignInOffer.NOT_BUILT
    }

    private fun keyForm(row: ProviderRosterRow, draft: ProviderConfigDraft): ProviderKeyForm {
        val verdict = verdictFor(draft.key, row.presentation?.keyPrefix)
        return ProviderKeyForm(
            draft = draft.key,
            revealed = draft.revealed,
            verdict = verdict,
            stage = when {
                draft.probing -> ProviderKeyForm.Stage.TESTING
                draft.stored -> ProviderKeyForm.Stage.CONNECTED
                else -> ProviderKeyForm.Stage.IDLE
            },
            problem = draft.problem,
            // The stored credential is what makes this a replacement, not the
            // draft: one credential per provider (decision 0029), so saving
            // over anything already filed replaces it whatever saved it.
            replacing = row.stored != null,
        )
    }

    /**
     * What the form can tell about a draft before spending a call on it.
     *
     * The prefix is the catalog's (`ProviderPresentation.keyPrefix`) and it is
     * optional, so a provider that published none has only the two answers a
     * field can reach on its own. Nothing here judges length or alphabet: a
     * vendor may change either without telling anybody, and a rule invented
     * here would refuse a working key.
     */
    fun verdictFor(draft: String, keyPrefix: String?): ProviderKeyForm.Verdict = when {
        draft.isBlank() -> ProviderKeyForm.Verdict.EMPTY
        keyPrefix != null && !draft.startsWith(keyPrefix) ->
            ProviderKeyForm.Verdict.PREFIX_MISMATCH

        else -> ProviderKeyForm.Verdict.PLAUSIBLE
    }

    /**
     * The card for a credential this page may manage, or null when there is
     * none to manage.
     *
     * Read from [availability] rather than from `row.stored` alone, and that
     * is the whole point: `row.stored` is the core's echo and the browser's
     * secure store is what actually holds the key, so a credential the core
     * has not echoed yet is `UNKNOWN` and not absent — exactly what
     * `ProviderRowDispatch.availabilityFor` says and what the connected list
     * already honours. Keying this card off the echo alone made the two
     * screens disagree about one fact: the list called the provider connected
     * while this page drew the bare paste form under it, with no way to sign
     * out and no way to change the model. A provider whose models are fetched
     * rather than published sits in that window every time the browser
     * starts, so for those it was not a window at all.
     *
     * With no echo there is nothing the vendor has told us — no account, no
     * plan — and a key is what the person pasted, so the card says unconfirmed
     * and names neither. The pinned model still reads from the row, because
     * the pin is the person's choice and not the vendor's echo.
     */
    private fun managedCredential(
        row: ProviderRosterRow,
        models: Map<String, List<ProviderModel>>,
        compiled: Boolean,
        availability: CredentialAvailability,
    ): ManagedCredential? {
        if (availability == CredentialAvailability.ABSENT) return null
        val stored = row.stored
        return ManagedCredential(
            accountLabel = stored?.accountLabel,
            planLabel = stored?.planLabel,
            subscriptionBacked = stored?.subscriptionBacked == true,
            confirmed = stored?.state == RosterCredentialState.USABLE,
            // The catalog's own name for the pinned model, never the
            // identifier: a model id is a wire value and reads like one.
            // A pin the catalog no longer carries shows as no pin, which is
            // what it now is.
            modelName = row.selectedModelId?.let { pinned ->
                models[row.providerId]?.firstOrNull { it.modelId == pinned }?.displayName
            },
            canReauthenticate = compiled && RosterAuthMethod.OAUTH in row.authMethods,
            lastRefusal = row.lastRefusal,
        )
    }

    /**
     * Where "Taffy uses this provider" stands for this one.
     *
     * Read against every row rather than this one alone, because the question
     * is about a choice between providers. The standing choice itself is
     * `ProviderRowDispatch.standingChoice`, so this page and the hub's badge
     * cannot disagree about which provider is in force.
     */
    private fun defaultChoice(
        row: ProviderRosterRow,
        availability: CredentialAvailability,
        rows: List<ProviderRosterRow>,
        browserHeldCredentialIds: Set<String>,
    ): ProviderDefaultChoice {
        if (!ProviderRowDispatch.configuredFor(row, availability)) return ProviderDefaultChoice.UNAVAILABLE
        val byProvider = rows.associate {
            it.providerId to ProviderRowDispatch.availabilityFor(it, browserHeldCredentialIds)
        }
        val standing = ProviderRowDispatch.standingChoice(byProvider, rows)
        val connected = ProviderRowDispatch.standingCandidates(rows, byProvider).size
        return when {
            standing == row.providerId -> ProviderDefaultChoice.IN_FORCE
            connected > 1 -> ProviderDefaultChoice.SHARED
            else -> ProviderDefaultChoice.OFFERED
        }
    }
}
