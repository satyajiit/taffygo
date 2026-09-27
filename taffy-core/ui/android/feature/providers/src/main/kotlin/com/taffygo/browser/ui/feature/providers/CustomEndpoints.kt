// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf

/**
 * The three things screen SCR-418 asks of an address a person typed (decision
 * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`).
 *
 * Stated in this feature's own vocabulary rather than the contract's, for the
 * reason `ProviderRosterRow` gives about the roster: a surface that named a
 * generated wire type would have to be edited every time the wire moved, and a
 * host test of the screen would have to build one. What crosses this seam is a
 * string a person typed, a name, a list of model identities, the runtime the
 * probe named — and, where a server asks for one, the *name of a sealed
 * credential record*. Never a key: a credential is a reference here as it is
 * everywhere (decision
 * `docs/decisions/0049-a-provider-credential-is-a-reference.md`), and the bytes
 * stop at the browser's secure store.
 */
interface CustomEndpoints {

    /**
     * The host each provider is currently set to, as the core published it.
     *
     * A host and not the address: it is what disclosure text shows, and its
     * contract promises no port and no path. [addresses] is the other half.
     */
    val hosts: Flow<Map<String, String>>

    /**
     * The whole address each provider of a person's own is registered at.
     *
     * Separate from [hosts] because they answer different questions. A host is
     * for showing; this is the exact string the browser registered, port and
     * base path included, so an edit screen can fill the field back in with
     * what a person typed instead of starting empty and explaining why. A
     * catalog provider carries neither, and is in neither map.
     */
    val addresses: Flow<Map<String, String>>

    /**
     * Ask what is at [address], before anything names it.
     *
     * Nothing durable changes, and naming [providerId] claims nothing: it is
     * the draft identity the verdict is filed under and the one the save will
     * reuse, so this screen matches its own answer by naming the row rather
     * than by watching for a row that was not there before.
     *
     * The answer is what the server said about itself, so zero models is an
     * answer rather than a failure — the address is right and nothing is loaded
     * behind it.
     *
     * [credentialHandle] names a sealed record and is absent for a server that
     * asks for nothing, which is the ordinary case. It exists because the check
     * can come back saying the server will list nothing without a key: a person
     * who then supplies one must be able to ask the same question again with it,
     * or the verdict names a problem they cannot act on.
     */
    suspend fun probe(
        address: String,
        providerId: String,
        credentialHandle: String?,
    ): CustomEndpointOutcome

    /**
     * Define the provider in one write: address, name, models and the runtime
     * the probe named, together (section 4).
     *
     * One command rather than two, so a provider can never exist
     * configured-but-unroutable through a half-completed pair. [models] is what
     * will be routable and is what the probe actually read — whole specs, so
     * nothing here has to invent a context window for a server it was told
     * about. An empty list is stated rather than defaulted, because a server
     * that listed nothing is a fact about the server.
     *
     * [credentialHandle] travels in the same command for the same reason, and
     * it is the name of a record the browser sealed rather than a key. Null is
     * a statement and not an omission: this endpoint needs no credential, which
     * is what a model server on somebody's own machine usually is. The core
     * reads it that way — a save naming no handle removes the credential the
     * provider had — so a caller that means "keep the one already there" says
     * so by naming it.
     */
    suspend fun save(
        providerId: String,
        displayName: String,
        address: String,
        models: List<CustomEndpointOutcome.Model>,
        server: CustomEndpointOutcome.ServerKind?,
        credentialHandle: String?,
    )

    /** Remove one provider the person defined. */
    suspend fun remove(providerId: String)

    /**
     * No endpoint of one's own can be registered on this build.
     *
     * A probe answers that nothing came back, which is the truth about a check
     * that was never run, and the two writes do nothing. Present so a preview
     * or a host test can stand a screen up without a core behind it.
     */
    class Absent : CustomEndpoints {
        override val hosts: Flow<Map<String, String>> = flowOf(emptyMap())
        override val addresses: Flow<Map<String, String>> = flowOf(emptyMap())

        override suspend fun probe(
            address: String,
            providerId: String,
            credentialHandle: String?,
        ): CustomEndpointOutcome =
            CustomEndpointOutcome.Refused(CustomEndpointOutcome.Problem.NO_ANSWER)

        override suspend fun save(
            providerId: String,
            displayName: String,
            address: String,
            models: List<CustomEndpointOutcome.Model>,
            server: CustomEndpointOutcome.ServerKind?,
            credentialHandle: String?,
        ) = Unit

        override suspend fun remove(providerId: String) = Unit
    }
}
