// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-419 — the providers this browser can reach a model through.
 *
 * One row per connected provider, saying what it is set to, and one way to
 * remove any of them. The hub (SCR-404) is where a provider is added; this is
 * where the ones that are already working are managed, which is why it lists
 * nothing a person has not connected.
 *
 * The sign-out is not written twice. The warning is `ProviderRemovalSheet`,
 * shared with screens SCR-415 and SCR-418, and the removal goes through the
 * same credential seam a provider's own page uses — so the two doors reach one
 * writer and say the same thing on the way.
 *
 * ## This screen is where the providers area is entered, and sometimes it is
 * not where the person stays
 *
 * Settings opens here rather than on the hub, because somebody with providers
 * set up came to see them and not to shop. Somebody with none came to add one,
 * and for them this screen has a list of nothing and one button — so it hands
 * over to SCR-404 instead of drawing that. The hand-over is `replaceCurrent`
 * rather than `goTo`: pushed, the hub's back would return here to be forwarded
 * straight out again, which is a back button that does not go back.
 */
@Composable
fun ConnectedProvidersScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ConnectedProvidersViewModel =
        screenViewModel(TaffyDestination.ConnectedProviders)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    // Only ever on a roster the core has published: `EMPTY` is reached through
    // `ready`, so a person is never forwarded on the strength of a list that
    // had not arrived. A navigator that does nothing — a preview, a host test —
    // leaves the empty state drawn, which is the truthful thing to draw.
    LaunchedEffect(state.handsOverToTheHub) {
        if (state.handsOverToTheHub) navigator.replaceCurrent(TaffyDestination.AiAndProviders)
    }

    ConnectedProvidersContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ConnectedProvidersContent(
    state: ConnectedProvidersUiState,
    onIntent: (ConnectedProvidersIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.ConnectedProviders,
        title = taffyString(R.string.taffy_providers_connected_title),
        onBack = onBack,
        modifier = modifier,
        // Adding is the one thing this screen does not do itself, so it sits
        // below the list rather than in it, and it stays put while the list
        // scrolls. It is drawn whatever the status: on a phone with nothing
        // connected this is the only control on the page, and it is the way
        // out of that state.
        footer = { AddProviderAction(onIntent = onIntent) },
        listModifier = if (state.status == ConnectedProvidersUiState.Status.READY) {
            Modifier.testTag(CONNECTED_LIST_TEST_TAG)
        } else {
            Modifier
        },
    ) {
        item(key = "intro", contentType = "intro") {
            Text(
                text = taffyString(R.string.taffy_providers_connected_intro),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
            )
        }
        when (state.status) {
            ConnectedProvidersUiState.Status.LOADING -> item(contentType = "loading") {
                ProviderHubSkeleton()
            }
            ConnectedProvidersUiState.Status.EMPTY -> item(contentType = "empty") {
                NothingConnected()
            }
            ConnectedProvidersUiState.Status.READY -> items(
                items = state.rows,
                key = ConnectedProviderRow::providerId,
                contentType = { "provider" },
            ) { row ->
                ConnectedRowItem(row = row, onIntent = onIntent)
            }
        }
    }
    state.confirming?.let { row ->
        ProviderRemovalSheet(
            title = taffyString(R.string.taffy_providers_signout_title, row.displayName),
            body = taffyString(R.string.taffy_providers_signout_body),
            confirmLabel = taffyString(R.string.taffy_providers_signout_confirm),
            running = row.signingOut,
            onConfirm = { onIntent(ConnectedProvidersIntent.ConfirmSignOut) },
            onCancel = { onIntent(ConnectedProvidersIntent.CancelSignOut) },
            testTag = CONNECTED_SIGN_OUT_SHEET_TEST_TAG,
            confirmTestTag = CONNECTED_SIGN_OUT_CONFIRM_TEST_TAG,
        )
    }
}

/**
 * The way on to SCR-404, where something new is connected.
 *
 * Primary rather than secondary: on a screen whose list is what a person came
 * to read, this is the only forward action, and on a screen with no list it is
 * the only action at all.
 */
@Composable
private fun AddProviderAction(onIntent: (ConnectedProvidersIntent) -> Unit) {
    TaffyPrimaryButton(
        label = taffyString(R.string.taffy_providers_connected_add),
        onClick = { onIntent(ConnectedProvidersIntent.AddProvider) },
        modifier = Modifier.fillMaxWidth(),
        icon = TaffyIcon.Plus,
        testTag = CONNECTED_ADD_TEST_TAG,
    )
}

/**
 * One connected provider: who it is, what it is set to, and one way out.
 *
 * The row itself leads to the surface that owns changing it — the provider's
 * page for a credential, the endpoint page for an address a person typed —
 * because this screen shows and removes and never edits.
 */
@Composable
private fun ConnectedRowItem(
    row: ConnectedProviderRow,
    onIntent: (ConnectedProvidersIntent) -> Unit,
) {
    val setting = rowSetting(row)
    val state = taffyString(rowStateRes(row))
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
    ) {
        TaffyListRow(
            title = row.displayName,
            supporting = setting,
            accessibleDescription = taffyString(
                R.string.taffy_providers_row,
                row.displayName,
                setting,
                state,
            ),
            testTag = "$CONNECTED_ROW_TEST_TAG_PREFIX${row.providerId}",
            onClick = { onIntent(ConnectedProvidersIntent.OpenRow(row)) },
            leading = { ProviderBadge(providerId = row.providerId, name = row.displayName) },
            trailing = {
                if (row.canSignOut) {
                    TaffyDangerButton(
                        label = taffyString(
                            if (row.signingOut) {
                                R.string.taffy_providers_managed_signing_out
                            } else {
                                R.string.taffy_providers_managed_sign_out
                            },
                        ),
                        onClick = { onIntent(ConnectedProvidersIntent.AskSignOut(row)) },
                        enabled = !row.signingOut,
                        loading = row.signingOut,
                        size = TaffyButtonSize.COMPACT,
                        testTag = "$CONNECTED_SIGN_OUT_TEST_TAG_PREFIX${row.providerId}",
                    )
                } else {
                    Text(
                        text = state,
                        style = TaffyTheme.typography.label,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
            },
        )
        // Under the setting rather than in place of the state: the row stays
        // connected, and what the vendor last refused is a fact beside that.
        row.lastRefusal?.let { refusal ->
            ProviderRefusalLine(
                refusal = refusal,
                displayName = row.displayName,
                testTag = "$CONNECTED_REFUSAL_TEST_TAG_PREFIX${row.providerId}",
            )
        }
    }
}

/**
 * Nothing is connected. Said as where everyone starts rather than as a
 * failure: no credential is missing, because none was ever stored.
 */
@Composable
private fun NothingConnected() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_connected_empty_title),
        body = taffyString(R.string.taffy_providers_connected_empty_body),
        leading = {
            Icon(
                imageVector = TaffyIcon.Cloud,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(ConnectedGlyphSize),
            )
        },
        modifier = Modifier.testTag(CONNECTED_EMPTY_TEST_TAG),
    )
}

/**
 * The row's supporting line: what a request sent here would actually be.
 *
 * The model comes first because it is the answer to the question this screen
 * exists for. The thinking rung joins it only where somebody asked for one —
 * Taffy deciding is the default and printing it on every row would be noise
 * that says nothing.
 */
@Composable
private fun rowSetting(row: ConnectedProviderRow): String {
    val model = row.modelName
        ?: taffyString(R.string.taffy_providers_managed_model_provider_order)
    val modelLine = taffyString(R.string.taffy_providers_managed_model, model)
    val thinking = row.thinking ?: return modelLine
    val thinkingLine = taffyString(
        R.string.taffy_providers_connected_thinking,
        taffyString(thinkingLabel(thinking)),
    )
    return taffyString(R.string.taffy_providers_label_pair, modelLine, thinkingLine)
}

/** One honest state per row, in the order it has to be read in. */
private fun rowStateRes(row: ConnectedProviderRow): Int = when {
    row.carriesStandingChoice -> R.string.taffy_providers_state_default
    row.availability == CredentialAvailability.PRESENT -> R.string.taffy_providers_state_connected
    row.ownEndpoint -> R.string.taffy_providers_way_endpoint
    else -> R.string.taffy_providers_state_unconfirmed
}

/** The tags screen SCR-419's semantics tests name. */
const val CONNECTED_LIST_TEST_TAG: String = "providers_connected_list"
const val CONNECTED_EMPTY_TEST_TAG: String = "providers_connected_empty"
const val CONNECTED_ADD_TEST_TAG: String = "providers_connected_add"
const val CONNECTED_ROW_TEST_TAG_PREFIX: String = "providers_connected_row_"
const val CONNECTED_REFUSAL_TEST_TAG_PREFIX: String = "providers_connected_refusal_"
const val CONNECTED_SIGN_OUT_TEST_TAG_PREFIX: String = "providers_connected_sign_out_"
const val CONNECTED_SIGN_OUT_SHEET_TEST_TAG: String = "providers_connected_sign_out_sheet"
const val CONNECTED_SIGN_OUT_CONFIRM_TEST_TAG: String = "providers_connected_sign_out_confirm"

private val ConnectedGlyphSize = 24.dp
