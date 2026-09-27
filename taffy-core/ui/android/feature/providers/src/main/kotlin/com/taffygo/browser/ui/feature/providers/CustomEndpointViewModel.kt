// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.credentials.ProviderCredentialsRepository
import com.taffygo.browser.ui.core.providers.ProviderRosterRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * Screen SCR-418's one source of truth.
 *
 * It holds one thing of its own — the draft — and reads everything else from
 * the published roster. The three consequential things a person can do here
 * (ask an address what it is, define the provider, remove it) each go through
 * the seam that owns them and come back as a published fact, so this screen
 * cannot report a provider the core has not filed.
 *
 * Nothing here rewrites an address. The probe answer can imply a different
 * one, and what this class does with that is put it in the draft as a
 * *proposal* and hold the save until a person answers it — because the
 * register's question is "did this person type this?" (decision 0096 section
 * 1) and a view model that corrected the address quietly would be answering it
 * on their behalf.
 *
 * The key a server may ask for is the one thing here that is neither the
 * core's answer nor the screen's own: it is sealed into the browser's secure
 * store first and travels as the *name* of that record. Sealing happens before
 * the two things that can carry it — the check and the write — and the write
 * carries it in the same command as the address, the name and the models,
 * because a provider defined and then separately given its key is the
 * half-completed pair decision 0096 section 4 exists to refuse.
 */
class CustomEndpointViewModel @Inject constructor(
    private val roster: ProviderRosterRepository,
    private val endpoints: CustomEndpoints,
    private val credentials: ProviderCredentialsRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    /** The provider this page is editing, or null while one is being added. */
    private val endpointId: String? = savedState.get<String>(TaffyDestination.ENDPOINT_ID)

    private val draft = MutableStateFlow(CustomEndpointDraft())

    /**
     * The draft beside the one browser fact the page cannot see any other way.
     *
     * Folded together rather than taken as a sixth stream, because they are
     * read at the same instant for the same page and the projection wants one
     * of them as a boolean about this provider.
     */
    private val local = combine(draft, credentials.configuredProviderIds) { draft, held ->
        draft to (endpointId != null && endpointId in held)
    }

    /** What screen SCR-418 renders. */
    val state: StateFlow<CustomEndpointUiState> = combine(
        roster.roster,
        roster.models,
        endpoints.hosts,
        endpoints.addresses,
        local,
    ) { rosterState, models, hosts, addresses, (localDraft, credentialHeld) ->
        CustomEndpointProjection.project(
            endpointId = endpointId,
            roster = rosterState,
            models = models,
            draft = localDraft,
            currentHost = endpointId?.let(hosts::get),
            currentAddress = endpointId?.let(addresses::get),
            credentialHeld = credentialHeld,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = CustomEndpointUiState(endpointId = endpointId),
        )

    /** Record the screen. The roster arrives by itself; nothing is loaded. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.CustomEndpointSetup(endpointId).screenId),
        )
    }

    /** Act on something the person did. */
    fun onIntent(intent: CustomEndpointIntent, navigator: TaffyNavigator) {
        when (intent) {
            CustomEndpointIntent.Probe -> probeAddress()
            CustomEndpointIntent.Save -> saveProvider(navigator)
            CustomEndpointIntent.ConfirmDelete -> removeProvider(navigator)
            CustomEndpointIntent.ChooseModel -> endpointId?.let {
                navigator.goTo(TaffyDestination.ModelSelection(it))
            }

            else -> draft.update { CustomEndpointReducer.reduce(it, intent) }
        }
    }

    /**
     * Ask the address what is there, and work out whether to propose another.
     *
     * The proposal is computed against the address that was actually probed
     * rather than whatever the field holds when the answer lands: a person who
     * kept typing while the check was out must not be shown a correction to a
     * string nobody tested.
     *
     * Pressing the check is also what settles two things the page could put off
     * until now. The address becomes the draft's own — a person acted on this
     * exact string, so the registered address must stop showing under it — and
     * the identity the verdict will be filed under is minted, once, and kept so
     * the save reuses it (decision 0096 section 5).
     *
     * The check carries a credential when there is one to carry, and that is
     * what makes a server answering "not without a key" something a person can
     * act on: they type the key, press check again, and the same question is
     * asked with it. A verdict that comes back asking for one opens the
     * advanced part of the page, so the field being asked for is on screen
     * rather than behind a control nobody has pressed.
     */
    private fun probeAddress() {
        val typed = state.value.address.trim()
        if (!state.value.probeActionable) return
        val identity = draft.value.providerId ?: endpointId
            ?: CustomEndpointProjection.mintProviderId(
                // The name field comes after the check on this page, so it is
                // usually still empty here. The machine they pointed at stands
                // in for it: an identity nobody reads is still better read as
                // the box it names than as a serial number.
                name = state.value.name.ifBlank { CustomEndpointAddress.hostOf(typed).orEmpty() },
                taken = roster.roster.value.rows.map { it.providerId }.toSet(),
            )
        draft.update {
            it.copy(
                address = typed,
                addressTouched = true,
                providerId = identity,
                probing = true,
                outcome = null,
                proposal = null,
                keptAsTyped = false,
                saveRefused = false,
                keySealRefused = false,
            )
        }
        viewModelScope.launch {
            val credentialHandle = try {
                credentialHandleFor(identity)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // The key never reached the store, so nothing was asked of the
                // server. Probing anyway would spend a check that could only
                // come back asking for the credential this one failed to seal.
                draft.update { it.copy(probing = false, keySealRefused = true) }
                return@launch
            }
            val outcome = endpoints.probe(typed, identity, credentialHandle)
            draft.update {
                if (it.address.trim() != typed) {
                    it.copy(probing = false)
                } else {
                    it.copy(
                        probing = false,
                        outcome = outcome,
                        proposal = CustomEndpointProjection.proposalFor(typed, outcome),
                        advancedOpen = it.advancedOpen || outcome.wantsACredential,
                    )
                }
            }
        }
    }

    /**
     * Define the provider in the one write decision 0096 section 4 asks for.
     *
     * The models travel with it, and they are the probe's own: whole specs read
     * off the server rather than names a person retyped. A server that listed
     * none is saved with an empty roster, stated rather than defaulted, because
     * that is what it said.
     *
     * The identity is the one the probe already named, so the verdict on the
     * page and the provider this write produces are the same row. Minting a
     * second one here would file the provider beside its own answer.
     *
     * The guards are repeated here rather than trusted from the screen: an
     * unanswered proposal must not become a saved address by way of a stale
     * button.
     *
     * The credential is sealed before the command and named by it. A key that
     * would not seal stops the save rather than filing a provider without it:
     * an endpoint that needs a credential and has none reaches the person's
     * server unauthenticated, which is worse than not being saved.
     */
    private fun saveProvider(navigator: TaffyNavigator) {
        val current = state.value
        val reached = current.reached ?: return
        if (!current.saveActionable) return
        val identity = current.endpointId ?: draft.value.providerId ?: return
        draft.update { it.copy(saving = true, saveRefused = false, keySealRefused = false) }
        viewModelScope.launch {
            val credentialHandle = try {
                credentialHandleFor(identity)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                draft.update { it.copy(saving = false, keySealRefused = true) }
                return@launch
            }
            try {
                endpoints.save(
                    providerId = identity,
                    displayName = current.name.trim(),
                    address = current.address.trim(),
                    models = reached.models,
                    server = reached.server,
                    credentialHandle = credentialHandle,
                )
                draft.update { it.copy(saving = false) }
                // The roster is live, so the provider appears on the page
                // behind this one by itself. Nothing here reports the save:
                // the next published snapshot does.
                navigator.goBack()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                draft.update { it.copy(saving = false, saveRefused = true) }
            }
        }
    }

    /**
     * The name of the record this endpoint's key is sealed in, or null.
     *
     * Three answers and one rule: whatever is in the field wins, and an empty
     * field changes nothing.
     *
     * A key that was typed is sealed under this page's identity and its handle
     * is returned — the bytes are built here, handed over, and zeroed whatever
     * happens. **An empty field is never an empty handle**: an empty handle is
     * a credential record that exists and resolves to nothing, which is the one
     * state the router refuses, and it would turn a working server that asks
     * for no key at all into a provider that cannot be reached.
     *
     * An empty field with a key already sealed for this provider answers that
     * key's handle, because the save states a provider whole: it is carrying
     * the credential it is not changing, not supplying a new one. Nothing can
     * show a stored key again, so an edit that only moved a port would
     * otherwise send no handle, and the core reads no handle as the person
     * removing the credential — an endpoint that had a key would quietly start
     * reaching their server with none.
     *
     * An empty field with nothing sealed is null, which is exactly what a
     * keyless server has always been saved and probed with.
     */
    private suspend fun credentialHandleFor(identity: String): String? {
        val typed = draft.value.key
        if (typed.isBlank()) return credentials.heldCredentialHandle(identity)
        val material = typed.toByteArray()
        return try {
            credentials.sealApiKey(identity, material)
        } finally {
            material.fill(0)
        }
    }

    /**
     * Remove the provider after the explicit second step.
     *
     * A removal that fails leaves the page where it was, which is the truthful
     * rendering of a provider that is in fact still filed.
     */
    private fun removeProvider(navigator: TaffyNavigator) {
        val identity = state.value.endpointId ?: return
        if (!draft.value.confirmingDelete || draft.value.deleting) return
        draft.update { it.copy(confirmingDelete = false, deleting = true) }
        viewModelScope.launch {
            try {
                endpoints.remove(identity)
                // The core has dropped the credential on the same command that
                // dropped the provider, so the sealed record it named is now a
                // key for a provider that does not exist. Discarded rather than
                // forgotten: a forget would send a second command naming a
                // provider the core no longer has, and this pair is the mirror
                // of the save's — one command carries both (decision 0096
                // section 4).
                credentials.discardSealedKey(identity)
                navigator.goBack()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                // No separate error channel says less than the page itself:
                // the provider is still on the roster behind it.
            } finally {
                draft.update { it.copy(deleting = false) }
            }
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}
