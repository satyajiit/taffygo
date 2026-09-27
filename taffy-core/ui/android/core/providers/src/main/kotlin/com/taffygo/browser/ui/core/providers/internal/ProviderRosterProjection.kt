// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.internal

import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.model.ModelInputModality
import com.taffygo.browser.ui.core.model.ModelRole
import com.taffygo.browser.ui.core.model.ProviderModel
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.model.ProviderRosterRow
import com.taffygo.browser.ui.core.model.ProviderRosterState
import com.taffygo.browser.ui.core.model.RosterAuthMethod
import com.taffygo.browser.ui.core.model.RosterCatalogLayer
import com.taffygo.browser.ui.core.model.RosterCredentialState
import com.taffygo.browser.ui.core.model.RosterProviderOrigin
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.RosterRefusalState
import com.taffygo.browser.ui.core.model.RosterStoredCredential
import com.taffygo.browser.ui.core.model.ThinkingLevel
import taffy.core_api.CatalogLayerView
import taffy.core_api.CoreStatus
import taffy.core_api.InputModalityView
import taffy.core_api.ModelRoleView
import taffy.core_api.ProviderAuthMethodView
import taffy.core_api.ProviderCredentialStateView
import taffy.core_api.ProviderModelView
import taffy.core_api.ProviderOriginView
import taffy.core_api.ProviderPresentationView
import taffy.core_api.ProviderRefusalStateView
import taffy.core_api.ProviderRefusalView
import taffy.core_api.ProviderRosterEntry
import taffy.core_api.StoredCredentialView
import taffy.core_api.ThinkingLevelView
import taffy.core_api.ThinkingPreferenceView

/**
 * One generated status snapshot, as the roster a surface reads.
 *
 * Pure so a host test can drive every mapping. [ProviderRosterState.ready]
 * is true only for a snapshot a ready core published: a STARTING core's
 * empty roster is "not told yet", not "nothing offered".
 */
internal fun CoreStatus.toRosterState(): ProviderRosterState = ProviderRosterState(
    ready = hasCompleteProjection(),
    rows = provider_roster.takeIf { hasCompleteProjection() }.orEmpty().map { it.toRosterRow() },
)

/**
 * The snapshot's flat model list, under the provider each model belongs to.
 *
 * The core orders the list by provider and then by catalog order, and grouping
 * preserves both: providers keep the order they were first named in, and each
 * provider's models keep the order the catalog gave them. A provider the core
 * named no model for has no entry at all, so a reader asks for its list and is
 * told nothing rather than told an empty catalog.
 */
internal fun CoreStatus.toModelsByProvider(): Map<String, List<ProviderModel>> =
    provider_models.takeIf { hasCompleteProjection() }.orEmpty()
        .groupBy({ it.provider_id }, { it.toProviderModel() })

internal fun ProviderRosterEntry.toRosterRow(): ProviderRosterRow = ProviderRosterRow(
    providerId = provider_id,
    displayName = display_name,
    origin = when (origin) {
        ProviderOriginView.CATALOG -> RosterProviderOrigin.CATALOG
        ProviderOriginView.CUSTOM -> RosterProviderOrigin.CUSTOM
    },
    authMethods = auth_methods.map { it.toRosterMethod() },
    stored = stored?.toRosterCredential(),
    signingIn = signing_in,
    enabled = enabled,
    configurable = configurable,
    subscription = subscription,
    catalogLayer = when (catalog_layer) {
        CatalogLayerView.EMBEDDED_BASELINE -> RosterCatalogLayer.EMBEDDED_BASELINE
        CatalogLayerView.REMOTE_OVERLAY -> RosterCatalogLayer.REMOTE_OVERLAY
        CatalogLayerView.USER_OVERRIDE -> RosterCatalogLayer.USER_OVERRIDE
    },
    selectedModelId = selected_model_id,
    thinking = thinking?.toThinkingLevel(),
    presentation = presentation?.toProviderPresentation(),
    endpointBase = endpoint_base,
    lastRefusal = last_refusal?.toRefusalState(),
    modelCount = model_count.toInt(),
    // `endpoint_changed` and `refused_endpoint_host` are read off the record
    // and dropped. They disclosed a served catalog's attempt to move a
    // provider's address that the core refused (decision 0115); decision 0200
    // leaves no served catalog, so the core sets them false and absent for
    // every row and will until the contract reset takes the fields. Projecting
    // a constant into a model surface would be a screen with a state nothing
    // can put it in.
)

/**
 * The vendor's last refusal, kept whole.
 *
 * Its own record rather than a member folded into the stored credential's
 * state, because the two answer different questions and a surface that read
 * one as the other would tell somebody their key had stopped working when the
 * vendor had merely stopped spending it.
 */
private fun ProviderRefusalStateView.toRefusalState(): RosterRefusalState = RosterRefusalState(
    refusal = when (refusal) {
        ProviderRefusalView.RATE_LIMIT -> RosterProviderRefusal.RATE_LIMIT
        ProviderRefusalView.BILLING -> RosterProviderRefusal.BILLING
        ProviderRefusalView.OVERLOADED -> RosterProviderRefusal.OVERLOADED
    },
    atMonotonicMs = at_monotonic_ms,
)

private fun ProviderAuthMethodView.toRosterMethod(): RosterAuthMethod = when (this) {
    ProviderAuthMethodView.API_KEY -> RosterAuthMethod.API_KEY
    ProviderAuthMethodView.OAUTH -> RosterAuthMethod.OAUTH
}

private fun StoredCredentialView.toRosterCredential(): RosterStoredCredential =
    RosterStoredCredential(
        authMethod = auth_method.toRosterMethod(),
        state = when (state) {
            ProviderCredentialStateView.USABLE -> RosterCredentialState.USABLE
            ProviderCredentialStateView.NEEDS_SIGN_IN -> RosterCredentialState.NEEDS_SIGN_IN
            ProviderCredentialStateView.REFRESH_FAILED -> RosterCredentialState.REFRESH_FAILED
        },
        subscriptionBacked = subscription_backed,
        accountLabel = account_label,
        planLabel = plan_label,
    )

private fun ThinkingPreferenceView.toThinkingLevel(): ThinkingLevel = level.toThinkingLevel()

private fun ProviderPresentationView.toProviderPresentation(): ProviderPresentation =
    ProviderPresentation(
        keyPrefix = key_prefix,
        getKeyUrl = get_key_url,
        docsUrl = docs_url,
    )

private fun ProviderModelView.toProviderModel(): ProviderModel = ProviderModel(
    providerId = provider_id,
    modelId = model_id,
    displayName = display_name,
    contextWindow = context_window,
    maxOutputTokens = max_output_tokens,
    reasoning = reasoning,
    toolCalling = tool_calling,
    roles = roles.map { it.toModelRole() },
    inputModalities = input_modalities.map { it.toInputModality() },
    thinkingLevels = thinking_levels.map { it.toThinkingLevel() },
)

private fun ModelRoleView.toModelRole(): ModelRole = when (this) {
    ModelRoleView.PRIMARY_REASONING -> ModelRole.PRIMARY_REASONING
    ModelRoleView.FAST_BROWSING -> ModelRole.FAST_BROWSING
    ModelRoleView.VISION -> ModelRole.VISION
    ModelRoleView.EMBEDDING -> ModelRole.EMBEDDING
}

private fun InputModalityView.toInputModality(): ModelInputModality = when (this) {
    InputModalityView.TEXT -> ModelInputModality.TEXT
    InputModalityView.IMAGE -> ModelInputModality.IMAGE
}

private fun ThinkingLevelView.toThinkingLevel(): ThinkingLevel = when (this) {
    ThinkingLevelView.OFF -> ThinkingLevel.OFF
    ThinkingLevelView.MINIMAL -> ThinkingLevel.MINIMAL
    ThinkingLevelView.LOW -> ThinkingLevel.LOW
    ThinkingLevelView.MEDIUM -> ThinkingLevel.MEDIUM
    ThinkingLevelView.HIGH -> ThinkingLevel.HIGH
    ThinkingLevelView.XHIGH -> ThinkingLevel.XHIGH
    ThinkingLevelView.MAX -> ThinkingLevel.MAX
}
