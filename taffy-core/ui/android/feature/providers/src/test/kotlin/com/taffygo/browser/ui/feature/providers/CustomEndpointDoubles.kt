// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The endpoint seam, modelled only as far as screen SCR-418 uses it.
 *
 * Its own file rather than an addition to `ProviderScreenDoubles`, because it
 * stands for a different seam: those doubles are the credential and roster
 * repositories screens SCR-415 and SCR-416 read, and this is the browser Core
 * API commands only this screen sends.
 */
internal class FakeCustomEndpoints(
    var answer: CustomEndpointOutcome = CustomEndpointOutcome.Reached(
        server = CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE,
        modelCount = 1,
        models = listOf(fakeModel("llama3.1:8b")),
        provedBase = null,
    ),
) : CustomEndpoints {

    private val publishedHosts = MutableStateFlow(emptyMap<String, String>())
    private val publishedAddresses = MutableStateFlow(emptyMap<String, String>())

    /** Every probe spent, whole, so a suite can prove which row it named. */
    val probed = mutableListOf<Ask>()

    /** Every write, whole, so a suite can prove what actually travelled. */
    val saved = mutableListOf<Write>()

    /** Every provider a removal was asked about. */
    val removed = mutableListOf<String>()

    /** Set to refuse the write, as a core that will not take it does. */
    var refusesSave: Boolean = false

    override val hosts: Flow<Map<String, String>> = publishedHosts.asStateFlow()
    override val addresses: Flow<Map<String, String>> = publishedAddresses.asStateFlow()

    fun publishHost(providerId: String, host: String) {
        publishedHosts.value = publishedHosts.value + (providerId to host)
    }

    fun publishAddress(providerId: String, address: String) {
        publishedAddresses.value = publishedAddresses.value + (providerId to address)
    }

    override suspend fun probe(
        address: String,
        providerId: String,
        credentialHandle: String?,
    ): CustomEndpointOutcome {
        probed += Ask(address, providerId, credentialHandle)
        return answer
    }

    override suspend fun save(
        providerId: String,
        displayName: String,
        address: String,
        models: List<CustomEndpointOutcome.Model>,
        server: CustomEndpointOutcome.ServerKind?,
        credentialHandle: String?,
    ) {
        if (refusesSave) error("the core refused the write")
        saved += Write(providerId, displayName, address, models, server, credentialHandle)
    }

    override suspend fun remove(providerId: String) {
        removed += providerId
    }

    /**
     * One probe, whole: the address it asked about, the row it named and the
     * credential it carried — null for the keyless server that is the ordinary
     * case, and never an empty handle.
     */
    data class Ask(
        val address: String,
        val providerId: String,
        val credentialHandle: String? = null,
    )

    /** One write, whole. A save that split it would agree with two commands. */
    data class Write(
        val providerId: String,
        val displayName: String,
        val address: String,
        val models: List<CustomEndpointOutcome.Model>,
        val server: CustomEndpointOutcome.ServerKind?,
        val credentialHandle: String? = null,
    )
}

/** One model as a probe reads it, with a working model's figures. */
internal fun fakeModel(
    modelId: String,
    contextWindow: UInt = 131_072u,
): CustomEndpointOutcome.Model = CustomEndpointOutcome.Model(
    modelId = modelId,
    displayName = modelId,
    contextWindow = contextWindow,
    maxOutputTokens = 8_192u,
    reasoning = false,
    toolCalling = true,
)
