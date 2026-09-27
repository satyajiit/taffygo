// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun ProviderHubLoadingPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderHubContent(state = ProviderHubUiState(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderHubPreview() {
    TaffyPreview(darkTheme = true) {
        ProviderHubContent(state = previewState(), onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun ProviderHubScaledPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderHubContent(state = previewState(), onIntent = {})
    }
}

/** The tab a person lands on with nothing connected: the keys. */
@ThemePreviews
@Composable
private fun ProviderHubKeyTabPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderHubContent(
            state = previewState().copy(showing = ProviderHubGroup.BRING_YOUR_OWN_KEY),
            onIntent = {},
        )
    }
}

/** A category that holds nothing, which says so rather than drawing blank. */
@ThemePreviews
@Composable
private fun ProviderHubEmptyTabPreview() {
    TaffyPreview(darkTheme = true) {
        ProviderHubContent(state = emptyCategoryState(), onIntent = {})
    }
}

/**
 * The screen with a fixture roster in place of the core's.
 *
 * These rows exercise the states the screen can draw — a connected provider
 * carrying the standing choice, a plan whose sign-in is unavailable here, a
 * plain key row, a person's own endpoint and a provider held shut — without
 * claiming to be the shipped catalog.
 */
private fun previewState(): ProviderHubUiState = ProviderHubProjection.project(
    ProviderRosterState(
        ready = true,
        rows = listOf(
            previewRow("anthropic", "Anthropic").copy(
                stored = RosterStoredCredential(
                    authMethod = RosterAuthMethod.API_KEY,
                    state = RosterCredentialState.USABLE,
                    subscriptionBacked = false,
                    accountLabel = "ada@example.com",
                    planLabel = null,
                ),
            ),
            previewRow(
                "kimi-coding",
                "Kimi For Coding",
                methods = listOf(RosterAuthMethod.OAUTH),
            ),
            previewRow("openai", "OpenAI"),
            previewRow("moonshot", "Moonshot", enabled = false),
            previewRow(
                "my-gateway",
                "My gateway",
                origin = RosterProviderOrigin.CUSTOM,
                catalogLayer = RosterCatalogLayer.USER_OVERRIDE,
            ),
        ),
    ),
    emptySet(),
    signInFlows = emptyMap(),
)

/**
 * A roster with nothing under two of the three tabs, standing on one of them.
 *
 * The state a first run reaches with a catalog of key-only providers: all three
 * tabs are drawn, two of them hold nothing, and the one being read explains
 * itself instead of drawing an empty column.
 */
private fun emptyCategoryState(): ProviderHubUiState = ProviderHubProjection.project(
    ProviderRosterState(ready = true, rows = listOf(previewRow("openai", "OpenAI"))),
    emptySet(),
    signInFlows = emptyMap(),
).copy(showing = ProviderHubGroup.SUBSCRIPTION)

private fun previewRow(
    providerId: String,
    displayName: String,
    methods: List<RosterAuthMethod> = listOf(RosterAuthMethod.API_KEY),
    origin: RosterProviderOrigin = RosterProviderOrigin.CATALOG,
    catalogLayer: RosterCatalogLayer = RosterCatalogLayer.EMBEDDED_BASELINE,
    enabled: Boolean = true,
): ProviderRosterRow = ProviderRosterRow(
    providerId = providerId,
    displayName = displayName,
    origin = origin,
    authMethods = methods,
    stored = null,
    signingIn = false,
    enabled = enabled,
    configurable = true,
    subscription = false,
    catalogLayer = catalogLayer,
    selectedModelId = null,
    thinking = null,
    presentation = null,
    endpointBase = null,
    lastRefusal = null,
    modelCount = 0,
)
