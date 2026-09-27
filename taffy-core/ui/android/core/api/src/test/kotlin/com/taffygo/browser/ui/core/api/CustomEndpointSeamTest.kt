// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CustomModelSpecView
import taffy.core_api.DetectedServerView
import taffy.core_api.ProviderWireApiView
import taffy.core_api.ServerKindView

/**
 * What the custom-endpoint seam has to carry for decision
 * `docs/decisions/0096-an-endpoint-a-person-typed-is-registered-with-the-browser.md`
 * section 4 to hold: a save arrives with its models and the runtime the probe
 * detected, in one write, and says so even when there are none of either.
 */
class CustomEndpointSeamTest {
    @Test
    fun `a save carries its models and its detected server`() = runTest {
        val client = RecordingCoreApiClient()

        client.saveCustomProvider(
            providerId = "workshop",
            displayName = "Workshop box",
            endpoint = "http://192.168.1.9:11434/v1",
            wireApi = ProviderWireApiView.OPEN_AI_COMPLETIONS,
            credentialHandle = null,
            models = listOf(SMALL, LARGE),
            detectedServer = DetectedServerView(ServerKindView.OLLAMA),
        )

        val saved = client.savedCustomProviders.single()
        assertEquals(listOf(SMALL, LARGE), saved.models)
        assertEquals(DetectedServerView(ServerKindView.OLLAMA), saved.detectedServer)
        // The address is passed through as it was typed, port and path
        // included, rather than reduced to an origin.
        assertEquals("http://192.168.1.9:11434/v1", saved.endpoint)
        assertNull(saved.credentialHandle)
    }

    @Test
    fun `a save with nothing found says so rather than saying nothing`() = runTest {
        val client = RecordingCoreApiClient()

        client.saveCustomProvider(
            providerId = "workshop",
            displayName = "Workshop box",
            endpoint = "https://models.example.test/v1",
            wireApi = ProviderWireApiView.OPEN_AI_COMPLETIONS,
            credentialHandle = "workshop",
            models = emptyList(),
            detectedServer = null,
        )

        val saved = client.savedCustomProviders.single()
        // Zero models is an answer: the address replied and nothing is loaded
        // behind it. It reaches the seam as an empty roster, not as a refusal.
        assertTrue(saved.models.isEmpty())
        // And the runtime is absent as a whole record, because the contract has
        // no member that means "no member".
        assertNull(saved.detectedServer)
        assertEquals("workshop", saved.credentialHandle)
    }

    @Test
    fun `each model keeps every figure the probe read`() = runTest {
        val client = RecordingCoreApiClient()

        client.saveCustomProvider(
            providerId = "workshop",
            displayName = "Workshop box",
            endpoint = "http://127.0.0.1:8000/v1",
            wireApi = ProviderWireApiView.OPEN_AI_COMPLETIONS,
            models = listOf(LARGE),
            detectedServer = DetectedServerView(ServerKindView.VLLM),
        )

        val model = client.savedCustomProviders.single().models.single()
        assertEquals("qwen3-32b", model.model_id)
        assertEquals("Qwen3 32B", model.display_name)
        assertEquals(131_072u, model.context_window)
        assertEquals(8_192u, model.max_output_tokens)
        assertTrue(model.reasoning)
        assertTrue(model.tool_calling)
    }

    /**
     * The probe names the draft identity its verdict is filed under, and the
     * save reuses it (decision 0096 section 5). Asking still writes nothing:
     * naming an identity is not claiming it.
     */
    @Test
    fun `a probe names the identity the save will reuse`() = runTest {
        val client = RecordingCoreApiClient()

        client.probeCustomEndpoint(
            endpoint = "http://taffy-box.local:11434",
            wireApi = ProviderWireApiView.OPEN_AI_COMPLETIONS,
            providerId = "taffy-box",
        )

        assertEquals(
            listOf(
                RecordingCoreApiClient.ProbedEndpoint(
                    endpoint = "http://taffy-box.local:11434",
                    wireApi = ProviderWireApiView.OPEN_AI_COMPLETIONS,
                    credentialHandle = null,
                    providerId = "taffy-box",
                ),
            ),
            client.probedEndpoints,
        )
        // Nothing was saved by asking.
        assertTrue(client.savedCustomProviders.isEmpty())
    }

    private companion object {
        val SMALL = CustomModelSpecView(
            model_id = "qwen3-4b",
            display_name = "Qwen3 4B",
            context_window = 32_768u,
            max_output_tokens = 4_096u,
            reasoning = false,
            tool_calling = true,
        )

        val LARGE = CustomModelSpecView(
            model_id = "qwen3-32b",
            display_name = "Qwen3 32B",
            context_window = 131_072u,
            max_output_tokens = 8_192u,
            reasoning = true,
            tool_calling = true,
        )
    }
}
