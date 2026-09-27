// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.ThinkingLevelView

/**
 * The provider roster: credentials filed by reference, provider sign-in,
 * probes of a key or an address, custom providers, and the standing model
 * choice. No parameter here carries key material.
 */
interface ProviderCoreApiClient {
    /**
     * File one provider's credential by reference.
     *
     * [credentialHandle] is the name the browser's own secure store gave the
     * record it holds, and it is the only thing about a credential that crosses
     * this seam: no parameter here carries a key, a token or a bearer, and that
     * is a property of the contract rather than of this declaration (decision
     * `docs/decisions/0049-a-provider-credential-is-a-reference.md`).
     */
    suspend fun saveProviderCredential(
        providerId: String,
        authMethod: ProviderAuthMethodView,
        credentialHandle: String,
    )

    /** Revoke one provider's credential and leave the provider itself defined. */
    suspend fun forgetProviderCredential(providerId: String)

    /**
     * File the browser layer's report of one stored credential's registry
     * state.
     *
     * Sent only from inside the provider credential critical section (decision
     * `docs/decisions/0078-a-provider-credential-has-one-writer.md`), after a
     * refresh or probe learned something the roster should show. The report
     * carries a name and an enum, never material, and it is registry fact
     * about a record that exists — the core refuses a state about nothing,
     * because absence is a deletion and travels as [forgetProviderCredential].
     */
    suspend fun setProviderCredentialState(
        providerId: String,
        state: ProviderCredentialStateView,
    )

    /**
     * Ask the core to prove one key with one bounded model call (decision
     * `docs/decisions/0083-a-pasted-key-is-proved-by-one-bounded-completion.md`).
     *
     * [credentialHandle] is opaque here as everywhere: the provider id itself
     * for a stored key, or the one-shot transient the browser's vault minted
     * for a pasted draft — spent by the send and never entering durable
     * records. Nothing durable changes on this command alone; the verdict
     * returns as the next [CoreApiClient.status] snapshot's `provider_probes`
     * row. One probe flies at a time — a second submission is refused as
     * backpressure.
     */
    suspend fun probeProviderKey(providerId: String, credentialHandle: String)

    /**
     * Ask what is at one address, before anything names it (decision
     * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
     * section 5).
     *
     * The same discipline as [probeProviderKey] and one difference. Nothing
     * durable changes on this command alone; the verdict returns on the next
     * [CoreApiClient.status] snapshot as a `provider_probes` row; one probe
     * flies at a time, so a second submission is refused as backpressure. What
     * differs is what the verdict is about — it proves an address rather than a
     * credential, and answers what is there rather than whether a key works.
     * Zero models is an answer: the address is right and nothing is loaded
     * behind it.
     *
     * The verdict carries the runtime the server named, **how many models it
     * listed and the models themselves**, and those are two separate facts all
     * the way to the surface: the count is what the server said and the list is
     * what survived the contract's bound, so a reader that took the list's
     * length for the count would report thirty-two models where fifty were
     * offered. It also carries the base the OpenAI-shaped API was actually
     * proved at, which is what stops a save filing an address that answered a
     * discovery path and serves no request.
     *
     * [providerId] is the draft identity the surface intends to save under, and
     * the verdict is filed on the row for it — so a screen matches its own
     * answer by naming it rather than by watching for a row that was not there
     * before. The save then reuses the same identity, which is what makes the
     * verdict and the provider it produced the same thing.
     *
     * [endpoint] is the address as the person typed it. [credentialHandle] is
     * opaque here as everywhere, and absent for a server that asks for nothing.
     */
    suspend fun probeCustomEndpoint(
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String? = null,
        providerId: String,
    )

    /**
     * Begin one provider sign-in.
     *
     * The surface names the provider and nothing else. The flow's identity and
     * its redirect binding are minted by the browser, exactly as they are for an
     * account sign-in, so a surface cannot replay one person's redirect into
     * another person's flow.
     */
    suspend fun startProviderAuth(providerId: String): String

    /** Cancel the exact live provider sign-in returned by [startProviderAuth]. */
    suspend fun cancelProviderAuth(flowId: String)

    /**
     * Define one custom provider in a single write.
     *
     * Endpoint, wire family, credential, models and detected runtime travel
     * together, so a provider can never exist configured-but-unauthenticated
     * through a half-completed pair of calls, nor filed with nothing behind it
     * to route to (decision
     * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
     * section 4).
     *
     * [endpoint] is the address the person typed. The browser registers that
     * exact string and afterwards accepts only that exact string back, so the
     * question asked of a later request is "did this person type this?" rather
     * than "is this address acceptable?" — which is why ports and paths are
     * allowed here where a catalog endpoint may carry neither. Cleartext
     * reaches only a literal local address; https reaches anything.
     *
     * [models] is what the probe found. An empty list is a saying rather than a
     * silence — the address answered and nothing is loaded behind it — so it is
     * stated rather than defaulted. Its length is bounded by the contract's
     * `MAX_CUSTOM_MODEL_ENTRIES` rather than by anything written here, because
     * a bound a caller can forget is not a bound. [detectedServer] is null when
     * the probe named no runtime: the whole record is absent, because the
     * contract has no member meaning "no member".
     */
    suspend fun saveCustomProvider(
        providerId: String,
        displayName: String,
        endpoint: String,
        wireApi: ProviderWireApiView,
        credentialHandle: String? = null,
        models: List<CustomModelSpecView>,
        detectedServer: DetectedServerView? = null,
    )

    /** Remove one custom provider entirely. */
    suspend fun removeCustomProvider(providerId: String)

    /**
     * State one person's standing choice for one provider, whole.
     *
     * Both [modelId] and [thinking] are the choice as it should now stand
     * rather than a change to apply, so passing null for either asks for the
     * default that null means: the provider's own model order, and Taffy
     * deciding how much thinking to ask for. A surface that sets a model
     * without naming a thinking level therefore says exactly what a person who
     * never opened that control meant, and cannot leave a stale rung standing.
     *
     * A model the provider does not carry is refused rather than stored, and
     * the accepted choice comes back on the next [CoreApiClient.status]
     * snapshot's roster row.
     */
    suspend fun setProviderModelPreference(
        providerId: String,
        modelId: String?,
        thinking: ThinkingLevelView?,
    )
}
