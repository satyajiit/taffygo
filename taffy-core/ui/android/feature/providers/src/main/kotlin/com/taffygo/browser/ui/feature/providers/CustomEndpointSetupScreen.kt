// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-418 — a provider whose address the person supplies.
 *
 * A model server they run themselves: Ollama on a laptop, LM Studio on a
 * desktop, vLLM or llama.cpp on a machine in the next room. Every one of those
 * is somewhere on a network, which is what decision
 * `docs/decisions/0060-no-on-device-model-runtime-is-selected.md` leaves as
 * the only shape this can have — nothing here runs a model on the phone, and
 * no line of copy on this page may suggest that it does.
 *
 * Adding one and editing one are the same screen and differ in the title,
 * because everything below the title is the same set of answers about the same
 * endpoint. Editing adds two things the answers cannot supply: which model this
 * provider is pinned to, and removing it.
 *
 * The page argues in one order and it is the order of the fields: what the
 * address is, what answered there, what to call it, and only then the write.
 * The key a server may ask for sits off that line, in the advanced part,
 * because most servers a person runs themselves ask for nothing and a required
 * field would say otherwise.
 */
@Composable
fun CustomEndpointSetupScreen(
    destination: TaffyDestination.CustomEndpointSetup,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: CustomEndpointViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    CustomEndpointSetupContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun CustomEndpointSetupContent(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.CustomEndpointSetup(state.endpointId),
        title = taffyString(
            if (state.editing) {
                R.string.taffy_providers_endpoint_edit_title
            } else {
                R.string.taffy_providers_endpoint_add_title
            },
        ),
        onBack = onBack,
        modifier = modifier,
    ) {
        when (state.status) {
            CustomEndpointUiState.Status.LOADING -> CustomEndpointSkeleton()
            CustomEndpointUiState.Status.UNKNOWN -> EndpointGoneState()
            CustomEndpointUiState.Status.READY -> CustomEndpointBody(state, onIntent)
        }
    }
    if (state.confirmingDelete) {
        RemoveEndpointSheet(state = state, onIntent = onIntent)
    }
}

/** The page in the order it argues in. */
@Composable
private fun CustomEndpointBody(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_BODY_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_intro),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        if (state.editing) {
            CurrentEndpointNote(state = state)
        }
        EndpointAddressSection(state = state, onIntent = onIntent)
        EndpointAdvancedSection(state = state, onIntent = onIntent)
        EndpointOutcomeSection(state = state, onIntent = onIntent)
        if (state.reached != null) {
            EndpointNameSection(state = state, onIntent = onIntent)
            EndpointSaveSection(state = state, onIntent = onIntent)
        }
        if (state.editing) {
            EndpointManagementSection(state = state, onIntent = onIntent)
        }
    }
}

/**
 * The deliberate second tap before an unrecoverable local removal.
 *
 * Its own surface rather than an inline confirmation, for the same reason the
 * sign-out sheet is: what is being confirmed is the removal of a record only
 * this phone holds.
 */
@Composable
private fun RemoveEndpointSheet(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    ProviderRemovalSheet(
        title = taffyString(R.string.taffy_providers_endpoint_delete_title, state.name),
        body = taffyString(R.string.taffy_providers_endpoint_delete_body),
        confirmLabel = taffyString(R.string.taffy_providers_endpoint_delete_confirm),
        running = state.deleting,
        onConfirm = { onIntent(CustomEndpointIntent.ConfirmDelete) },
        onCancel = { onIntent(CustomEndpointIntent.CancelDelete) },
        testTag = CUSTOM_ENDPOINT_REMOVE_SHEET_TEST_TAG,
        confirmTestTag = CUSTOM_ENDPOINT_REMOVE_CONFIRM_TEST_TAG,
    )
}

/**
 * What a saved provider is pointing at.
 *
 * The field below now starts at the address the browser registered, because
 * Core API 3.18 carries it on the roster row beside the host. This line stays
 * anyway: the field can be edited and the note cannot, so a person who has
 * begun retyping the address can still see where the provider points until
 * they save.
 */
@Composable
private fun CurrentEndpointNote(state: CustomEndpointUiState) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        state.currentHost?.let { host ->
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_current, host),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_CURRENT_TEST_TAG),
            )
        }
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_current_note),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The two things editing adds: which model it is set to, and removing it. */
@Composable
private fun EndpointManagementSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_MANAGE_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(
                R.string.taffy_providers_managed_model,
                state.pinnedModelName
                    ?: taffyString(R.string.taffy_providers_managed_model_provider_order),
            ),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        // The model list is screen SCR-417 and is not written twice: choosing
        // one for a provider a person defined is the same act as choosing one
        // for a provider the catalog served.
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_providers_managed_change_model),
            onClick = { onIntent(CustomEndpointIntent.ChooseModel) },
            enabled = !state.busy,
            icon = TaffyIcon.SlidersHorizontal,
            size = TaffyButtonSize.COMPACT,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_CHANGE_MODEL_TEST_TAG,
        )
        TaffyDangerButton(
            label = taffyString(
                if (state.deleting) {
                    R.string.taffy_providers_endpoint_deleting
                } else {
                    R.string.taffy_providers_endpoint_delete
                },
            ),
            onClick = { onIntent(CustomEndpointIntent.AskDelete) },
            enabled = !state.busy,
            loading = state.deleting,
            size = TaffyButtonSize.COMPACT,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_REMOVE_TEST_TAG,
        )
    }
}

/**
 * The roster no longer carries this provider.
 *
 * Said as a fact about the list rather than as a failure, in the same words
 * screen SCR-415 uses for the same situation — one sentence about a provider
 * that has left the roster, not two that could drift apart.
 */
@Composable
private fun EndpointGoneState() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_config_gone_title),
        body = taffyString(R.string.taffy_providers_config_gone_body),
        leading = {
            Icon(
                imageVector = TaffyIcon.Cloud,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(EndpointGlyphSize),
            )
        },
        modifier = Modifier.testTag(CUSTOM_ENDPOINT_GONE_TEST_TAG),
    )
}

/** The shape of the page before the roster has arrived. */
@Composable
private fun CustomEndpointSkeleton() {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_LOADING_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SkeletonFieldHeight),
            shape = TaffyTheme.shapes.card,
            accessibleDescription = taffyString(R.string.taffy_providers_config_loading),
        )
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SkeletonBodyHeight),
            shape = TaffyTheme.shapes.card,
        )
    }
}

/** The tags screen SCR-418's semantics tests name. */
const val CUSTOM_ENDPOINT_BODY_TEST_TAG: String = "providers_endpoint_body"
const val CUSTOM_ENDPOINT_LOADING_TEST_TAG: String = "providers_endpoint_loading"
const val CUSTOM_ENDPOINT_GONE_TEST_TAG: String = "providers_endpoint_gone"
const val CUSTOM_ENDPOINT_CURRENT_TEST_TAG: String = "providers_endpoint_current"
const val CUSTOM_ENDPOINT_MANAGE_TEST_TAG: String = "providers_endpoint_manage"
const val CUSTOM_ENDPOINT_CHANGE_MODEL_TEST_TAG: String = "providers_endpoint_change_model"
const val CUSTOM_ENDPOINT_REMOVE_TEST_TAG: String = "providers_endpoint_remove"
const val CUSTOM_ENDPOINT_REMOVE_SHEET_TEST_TAG: String = "providers_endpoint_remove_sheet"
const val CUSTOM_ENDPOINT_REMOVE_CONFIRM_TEST_TAG: String = "providers_endpoint_remove_confirm"

private val EndpointGlyphSize = 24.dp
private val SkeletonFieldHeight = 72.dp
private val SkeletonBodyHeight = 180.dp
