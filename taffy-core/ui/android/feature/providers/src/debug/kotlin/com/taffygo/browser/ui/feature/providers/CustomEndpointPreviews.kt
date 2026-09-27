// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun CustomEndpointBlankPreview() {
    TaffyPreview(darkTheme = false) {
        CustomEndpointSetupContent(
            state = CustomEndpointUiState(status = CustomEndpointUiState.Status.READY),
            onIntent = {},
        )
    }
}

/**
 * The state this screen exists for: reached, four models, and a corrected
 * address waiting on a person.
 */
@ThemePreviews
@Composable
private fun CustomEndpointProposalPreview() {
    TaffyPreview(darkTheme = true) {
        CustomEndpointSetupContent(state = proposing(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun CustomEndpointNoModelsPreview() {
    TaffyPreview(darkTheme = false) {
        CustomEndpointSetupContent(
            state = proposing().copy(
                address = "http://192.168.1.9:11434/v1",
                proposal = null,
                outcome = CustomEndpointOutcome.Reached(
                    server = CustomEndpointOutcome.ServerKind.OLLAMA,
                    modelCount = 0,
                    models = emptyList(),
                    provedBase = "http://192.168.1.9:11434/v1",
                ),
            ),
            onIntent = {},
        )
    }
}

/**
 * A server that will list nothing without a key, with the advanced part open
 * where the check left it and the field the sentence asks for on screen.
 */
@ThemePreviews
@Composable
private fun CustomEndpointWantsAKeyPreview() {
    TaffyPreview(darkTheme = false) {
        CustomEndpointSetupContent(
            state = proposing().copy(
                address = "https://models.example.test/v1",
                cleartext = false,
                proposal = null,
                outcome = CustomEndpointOutcome.Refused(
                    CustomEndpointOutcome.Problem.WANTS_A_CREDENTIAL,
                ),
                advancedOpen = true,
                key = "sk-not-a-real-key",
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun CustomEndpointUnreachablePreview() {
    TaffyPreview(darkTheme = false) {
        CustomEndpointSetupContent(
            state = proposing().copy(
                proposal = null,
                outcome = CustomEndpointOutcome.Refused(
                    CustomEndpointOutcome.Problem.NOTHING_ANSWERED,
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun CustomEndpointScaledPreview() {
    TaffyPreview(darkTheme = false) {
        CustomEndpointSetupContent(state = proposing(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ConnectedProvidersPreview() {
    TaffyPreview(darkTheme = false) {
        ConnectedProvidersContent(state = connectedList(), onIntent = {})
    }
}

/**
 * The empty list, which nobody reaches on a phone.
 *
 * `ConnectedProvidersScreen` hands over to the hub the moment a published
 * roster carries nothing connected, so this state exists only in the stateless
 * half. It is previewed anyway because that half is what a host test and a
 * two-pane host both draw, and a composable nobody has looked at is where the
 * next layout defect lives.
 */
@ThemePreviews
@Composable
private fun ConnectedProvidersEmptyPreview() {
    TaffyPreview(darkTheme = true) {
        ConnectedProvidersContent(
            state = ConnectedProvidersUiState(status = ConnectedProvidersUiState.Status.EMPTY),
            onIntent = {},
        )
    }
}

/** A bare origin that answered, with the version segment still to settle. */
private fun proposing(): CustomEndpointUiState = CustomEndpointUiState(
    status = CustomEndpointUiState.Status.READY,
    address = "http://192.168.1.9:11434",
    name = "The laptop",
    cleartext = true,
    outcome = CustomEndpointOutcome.Reached(
        server = CustomEndpointOutcome.ServerKind.OLLAMA,
        modelCount = 4,
        models = listOf(
            previewModel("llama3.1:8b", "Llama 3.1 8B"),
            previewModel("qwen3:4b", "Qwen3 4B"),
            previewModel("mistral:7b", "Mistral 7B"),
            previewModel("phi4:14b", "Phi-4 14B"),
        ),
        provedBase = "http://192.168.1.9:11434/v1",
    ),
    proposal = "http://192.168.1.9:11434/v1",
)

/** One model as a probe of a local runtime reads it. */
private fun previewModel(modelId: String, displayName: String): CustomEndpointOutcome.Model =
    CustomEndpointOutcome.Model(
        modelId = modelId,
        displayName = displayName,
        contextWindow = 131_072u,
        maxOutputTokens = 8_192u,
        reasoning = false,
        toolCalling = true,
    )

/** One catalog provider and one the person defined, both connected. */
private fun connectedList(): ConnectedProvidersUiState = ConnectedProvidersUiState(
    status = ConnectedProvidersUiState.Status.READY,
    rows = listOf(
        ConnectedProviderRow(
            providerId = "example-vendor",
            displayName = "Example vendor",
            availability = CredentialAvailability.PRESENT,
            accountLabel = "work@example.test",
            planLabel = null,
            modelName = "A capable model",
            thinking = ThinkingLevel.MEDIUM,
            ownEndpoint = false,
            carriesStandingChoice = true,
            canSignOut = true,
            signingOut = false,
            offer = ProviderRowOffer.Configure,
        ),
        ConnectedProviderRow(
            providerId = "the-laptop",
            displayName = "The laptop",
            availability = CredentialAvailability.UNKNOWN,
            accountLabel = null,
            planLabel = null,
            modelName = null,
            thinking = null,
            ownEndpoint = true,
            carriesStandingChoice = false,
            canSignOut = false,
            signingOut = false,
            offer = ProviderRowOffer.EditEndpoint,
        ),
    ),
)
