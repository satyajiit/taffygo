// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.ui.TaffyDestination

/**
 * What one roster row offers, where it is filed, and where pressing it goes.
 *
 * ## No rule here reads a provider's identity
 *
 * The merged catalog is served (decision 0080), so the set of providers changes
 * without a release: a screen that decided anything by naming `"anthropic"` or
 * `"openrouter"` would be correct only for the catalog it was compiled beside,
 * and silently wrong for the next one. Every answer below is read off the facts
 * the roster states about a row — its methods, whether this build can act on
 * it, whether a credential is stored, whether the catalog holds it shut,
 * whether its address moved, which layer supplied it.
 *
 * The one place a provider id appears is [offerFor]'s lookup in the compiled
 * sign-in flow map, and that is a fact about *this binary* rather than about
 * the provider: a flow names pinned origins and a shape, which a served catalog
 * must not be able to invent (decision 0081). A vendor the catalog offers OAUTH
 * for and this build cannot sign in to is listed and explained — never turned
 * into a key form, which would be a control that cannot work.
 */
object ProviderRowDispatch {

    /**
     * What this row offers, decided in precedence order.
     *
     * The three refusals come first because each is a reason the product has
     * already given for not using the provider as offered, and a person should
     * not be led into setting up something the product has just declined. Then
     * the person's own endpoint, because it is edited where it was typed. Then
     * a stored credential, because something stored is something to manage and
     * management lives on the provider's own page whatever method saved it.
     * Only then the methods on offer.
     */
    fun offerFor(
        row: ProviderRosterRow,
        signInFlows: Map<String, Boolean>,
    ): ProviderRowOffer = offerFor(row, signInFlows, group = null)

    /**
     * What this row offers *when asked from one group*.
     *
     * A provider naming both a plan and a key is filed under both (see
     * [groupsFor]), and the same row cannot answer both questions with one
     * offer: under Subscription the way in is the vendor's sign-in, under
     * Bring your own key it is the key form. So the group asking is part of
     * the question. Passing `null` asks for the row's primary way in, which is
     * what a surface with no groups — a picker, a connected list — wants.
     *
     * The four refusals still come first and in the same order, because each
     * is a reason the product has already declined the provider outright, and
     * no group makes a declined provider actionable.
     */
    fun offerFor(
        row: ProviderRosterRow,
        signInFlows: Map<String, Boolean>,
        group: ProviderHubGroup?,
    ): ProviderRowOffer = when {
        !row.configurable -> ProviderRowOffer.Blocked(ProviderRowOffer.Reason.NOT_ACTIONABLE)
        !row.enabled -> ProviderRowOffer.Blocked(ProviderRowOffer.Reason.HELD_SHUT)
        row.isTheirOwn() -> ProviderRowOffer.EditEndpoint
        row.stored != null -> ProviderRowOffer.Configure
        group == ProviderHubGroup.SUBSCRIPTION -> signInOffer(row, signInFlows)
        RosterAuthMethod.API_KEY in row.authMethods -> ProviderRowOffer.Configure
        else -> signInOffer(row, signInFlows)
    }

    /**
     * The plan half of [offerFor], which the Subscription group always asks.
     *
     * Three answers rather than two, because "this binary carries a flow" and
     * "the browser will start it" are different facts. A flow can be complete,
     * tested and compiled and still be refused at the moment it begins, when
     * nobody has dated a review of what the vendor's terms permit — the state
     * every compiled vendor was in until decision 0113 dated the rows.
     * Offering Sign in for those put a control in front of somebody that
     * answered "not available on this device" when pressed, which described
     * neither what had happened nor anything they could do about it.
     */
    private fun signInOffer(
        row: ProviderRosterRow,
        signInFlows: Map<String, Boolean>,
    ): ProviderRowOffer = when {
        // The roster describes the provider and the catalog is served, so a
        // vendor can stop offering OAUTH between releases while this binary
        // still carries its flow. There is then nothing to sign in with,
        // whatever the flow map says.
        RosterAuthMethod.OAUTH !in row.authMethods ->
            ProviderRowOffer.Blocked(ProviderRowOffer.Reason.NO_METHOD)

        row.providerId !in signInFlows ->
            ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_BUILT)

        signInFlows[row.providerId] == true -> ProviderRowOffer.SignIn
        else -> ProviderRowOffer.Blocked(ProviderRowOffer.Reason.SIGN_IN_NOT_CLEARED)
    }

    /**
     * Whether a credential stands behind this provider.
     *
     * Both halves of credential storage are read, and they may disagree.
     * The core's roster is the echo — it is what a save, a sign-in or a forget
     * eventually reports — and [browserHeldCredentialIds] is what the browser's
     * secure store holds right now. A handle the roster has not echoed is
     * [CredentialAvailability.UNKNOWN] rather than absent, so the window
     * between the two never re-locks a row that is working.
     */
    fun availabilityFor(
        row: ProviderRosterRow,
        browserHeldCredentialIds: Set<String>,
    ): CredentialAvailability {
        val stored = row.stored
        return when {
            stored == null && row.providerId !in browserHeldCredentialIds ->
                CredentialAvailability.ABSENT

            stored == null -> CredentialAvailability.UNKNOWN
            stored.state == RosterCredentialState.USABLE -> CredentialAvailability.PRESENT
            else -> CredentialAvailability.UNKNOWN
        }
    }

    /**
     * Whether this provider has been set up, without claiming a request worked.
     *
     * A saved own endpoint can require no credential (decision 0096). Its
     * published address is the configuration; a custom identity alone is not.
     * Keep credential availability separate so this never invents a key or
     * offers signing out of one that does not exist.
     */
    fun configuredFor(row: ProviderRosterRow, availability: CredentialAvailability): Boolean =
        availability != CredentialAvailability.ABSENT ||
            (row.isTheirOwn() && !row.endpointBase.isNullOrBlank())

    /** A stored credential of uncertain usability never becomes a keyless route. */
    fun standingCandidates(
        rows: List<ProviderRosterRow>,
        availability: Map<String, CredentialAvailability>,
    ): List<String> = rows.filter { row ->
        when (val credential = availability.getValue(row.providerId)) {
            CredentialAvailability.PRESENT -> true
            CredentialAvailability.UNKNOWN -> false
            CredentialAvailability.ABSENT -> row.modelCount > 0 && configuredFor(row, credential)
        }
    }.map { it.providerId }

    /**
     * The provider a model request would go to right now, or null when the
     * product cannot honestly name one.
     *
     * Nothing in the roster names a default provider, so this is derived, and
     * it is derived conservatively. One usable credential or saved keyless
     * endpoint is unambiguous; two are not, and the product holds no fact that
     * settles which is in force. Naming neither is the truthful answer; naming
     * both would say something the product does not know.
     *
     * One function, because two surfaces ask it. Screen SCR-404 badges a row
     * with it and screen SCR-415 states it on the provider's own page, and a
     * rule written twice is a rule that ends up answering differently on two
     * screens a person reads one after the other.
     */
    fun standingChoice(
        availability: Map<String, CredentialAvailability>,
        rows: List<ProviderRosterRow>,
    ): String? = standingCandidates(rows, availability).singleOrNull()

    /**
     * Which groups this row is filed under — one way in, one entry.
     *
     * ## A connected provider is filed under nothing
     *
     * SCR-404 is the screen a provider is added on, so a provider that is
     * already working is not on it. The empty list is the whole of that rule,
     * and it is stated here rather than as a filter beside the projection so
     * that the hub and `ConnectedProvidersProjection` cannot drift into
     * disagreeing about what "connected" means: this returns nothing for
     * exactly the rows that one returns.
     *
     * The person's own address outranks the rest of the filing, because an
     * endpoint they typed is described by where it points rather than by what
     * its catalog entry says it accepts.
     *
     * Otherwise a row is filed under **every** way in it actually offers, and
     * a provider naming both a plan and a key is therefore in two groups. It
     * used to be filed with the keys alone, on the reasoning that a key is the
     * way it can be finished today and the provider's own page offers the plan
     * as well. That reasoning was wrong in the way that matters: somebody who
     * pays for a plan opens Subscription, and a vendor whose plan this build
     * can sign in to was not there. A tab named for a way in must hold every
     * provider offering that way in, or it is not a filing rule but a guess
     * about which way in a person wants.
     *
     * A row naming no method at all is filed with the keys and says so on its
     * face — that is a catalog defect, and hiding it in a fourth group would
     * only make it harder to notice.
     */
    fun groupsFor(
        row: ProviderRosterRow,
        availability: CredentialAvailability,
    ): List<ProviderHubGroup> = when {
        configuredFor(row, availability) -> emptyList()
        row.isTheirOwn() -> listOf(ProviderHubGroup.YOUR_OWN_ENDPOINT)
        else -> buildList {
            // The catalog's `subscription`, not `OAUTH` alone. A vendor whose
            // exchange mints a metered key offers a real sign-in and belongs
            // with the keys it is billed like — filing it under a tab named
            // for plans would be the same mistake in the other direction.
            if (RosterAuthMethod.OAUTH in row.authMethods && row.subscription) {
                add(ProviderHubGroup.SUBSCRIPTION)
            }
            if (RosterAuthMethod.API_KEY in row.authMethods) {
                add(ProviderHubGroup.BRING_YOUR_OWN_KEY)
            }
            if (isEmpty()) add(ProviderHubGroup.BRING_YOUR_OWN_KEY)
        }
    }

    /**
     * How this provider could be reached, as one answer rather than a list.
     *
     * The person's own address outranks the methods, because an endpoint they
     * typed is described by where it points and not by what its catalog entry
     * happens to say it accepts.
     */
    fun wayInFor(row: ProviderRosterRow): ProviderWayIn {
        val key = RosterAuthMethod.API_KEY in row.authMethods
        val plan = RosterAuthMethod.OAUTH in row.authMethods
        return when {
            row.isTheirOwn() -> ProviderWayIn.OWN_ENDPOINT
            key && plan -> ProviderWayIn.KEY_OR_PLAN
            key -> ProviderWayIn.KEY
            plan -> ProviderWayIn.PLAN
            else -> ProviderWayIn.NONE
        }
    }

    /**
     * Where pressing this row goes, or null when it cannot be pressed.
     *
     * Null is the whole of a blocked row's press behaviour: the screen gives
     * such a row no click action rather than opening a surface that would
     * refuse it a moment later.
     */
    fun destinationFor(row: ProviderHubRow): TaffyDestination? =
        destinationFor(row.providerId, row.offer)

    /**
     * The same rule, asked with the two facts it actually reads.
     *
     * Screen SCR-417 lists models behind providers that are not set up yet, and
     * a press on one has to arrive at the place that thing can be supplied —
     * which is the provider's own page for a key, the vendor's sign-in for a
     * plan, and the endpoint page for an address a person typed. Sending every
     * one of them to the key form would offer a control that cannot work on
     * exactly the rows that most need a working one.
     */
    fun destinationFor(providerId: String, offer: ProviderRowOffer): TaffyDestination? =
        when (offer) {
            ProviderRowOffer.Configure -> TaffyDestination.ProviderConfig(providerId)
            ProviderRowOffer.SignIn -> TaffyDestination.ProviderSignIn(providerId)
            ProviderRowOffer.EditEndpoint -> TaffyDestination.CustomEndpointSetup(providerId)
            is ProviderRowOffer.Blocked -> null
        }

    /**
     * Whether the person supplied this provider themselves.
     *
     * Two independent fields say so, and either is enough. Reading only one
     * risks filing a person's own endpoint under the keys and sending them to
     * a key form for an address they typed; reading both can at worst send a
     * catalog row to the endpoint page, which shows them what it is.
     */
    private fun ProviderRosterRow.isTheirOwn(): Boolean =
        origin == RosterProviderOrigin.CUSTOM || catalogLayer == RosterCatalogLayer.USER_OVERRIDE
}
