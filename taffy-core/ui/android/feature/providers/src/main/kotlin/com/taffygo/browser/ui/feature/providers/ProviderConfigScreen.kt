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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-415 — one provider's own page.
 *
 * The page that actually changes something. Everything about one provider that
 * a person can decide lives here: the credential, the model it is pinned to,
 * and whether Taffy's own requests go this way. The hub (SCR-404) only chooses
 * which provider is being talked about.
 *
 * One primary road at a time. Where a provider offers both a plan and a key,
 * the plan is a block of its own above a rule that reads "or paste a key", and
 * the key form is below it — never two full-width primary buttons side by side,
 * which is how a person ends up pasting a key into a provider they could have
 * signed in to in one tap.
 */
@Composable
fun ProviderConfigScreen(
    destination: TaffyDestination.ProviderConfig,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ProviderConfigViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    ProviderConfigContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ProviderConfigContent(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.ProviderConfig(state.providerId),
        title = state.displayName.ifBlank {
            taffyString(R.string.taffy_providers_config_title)
        },
        onBack = onBack,
        modifier = modifier,
    ) {
        when (state.status) {
            ProviderConfigUiState.Status.LOADING -> ProviderConfigSkeleton()
            ProviderConfigUiState.Status.UNKNOWN -> ProviderGoneState()
            ProviderConfigUiState.Status.READY -> ProviderConfigBody(state, onIntent)
        }
    }
    if (state.confirmingSignOut) {
        SignOutSheet(state = state, onIntent = onIntent)
    }
}

/**
 * The page in the order it argues in: what is already connected, then the ways
 * in, then what the credential is for.
 */
@Composable
private fun ProviderConfigBody(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_CONFIG_BODY_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        ProviderConfigHeader(state = state)
        val blocked = state.blocked
        if (blocked != null) {
            ProviderBlockedNotice(reason = blocked)
            return@Column
        }
        state.managed?.let { ManagedCredentialCard(managed = it, state = state, onIntent = onIntent) }
        when (state.signIn) {
            ProviderSignInOffer.OFFERED -> SignInBlock(state = state, onIntent = onIntent)
            ProviderSignInOffer.NOT_BUILT -> SignInNotBuiltNotice(state.displayName)
            ProviderSignInOffer.NONE -> Unit
        }
        if (state.separatesWaysIn) {
            OrPasteAKeyRule()
        }
        state.keyForm?.let {
            ProviderKeySection(form = it, state = state, onIntent = onIntent)
        }
        ProviderDefaultSection(state = state, onIntent = onIntent)
    }
}

/**
 * The deliberate second tap before an unrecoverable local deletion.
 *
 * The sheet itself is `ProviderRemovalSheet`, shared with screens SCR-418 and
 * SCR-419: three screens remove something a person can only have once, and the
 * warning they read must not depend on which of the three they arrived from.
 * This function is what that sheet says about a credential.
 */
@Composable
private fun SignOutSheet(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    ProviderRemovalSheet(
        title = taffyString(R.string.taffy_providers_signout_title, state.displayName),
        body = taffyString(R.string.taffy_providers_signout_body),
        confirmLabel = taffyString(R.string.taffy_providers_signout_confirm),
        running = state.signingOut,
        onConfirm = { onIntent(ProviderConfigIntent.ConfirmSignOut) },
        onCancel = { onIntent(ProviderConfigIntent.CancelSignOut) },
        testTag = PROVIDER_SIGN_OUT_SHEET_TEST_TAG,
        confirmTestTag = PROVIDER_SIGN_OUT_CONFIRM_TEST_TAG,
    )
}

/**
 * The catalog no longer carries this provider.
 *
 * Said as a fact about the list rather than as a failure: the catalog is served
 * and a provider can leave it between one snapshot and the next, and nothing
 * has been taken off the phone.
 */
@Composable
private fun ProviderGoneState() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_config_gone_title),
        body = taffyString(R.string.taffy_providers_config_gone_body),
        leading = {
            Icon(
                imageVector = TaffyIcon.Cloud,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(GlyphSize),
            )
        },
        modifier = Modifier.testTag(PROVIDER_CONFIG_GONE_TEST_TAG),
    )
}

/** The shape of the page before the roster has arrived. */
@Composable
private fun ProviderConfigSkeleton() {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_CONFIG_LOADING_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SkeletonHeaderHeight),
            shape = TaffyTheme.shapes.card,
            accessibleDescription = taffyString(R.string.taffy_providers_config_loading),
        )
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SkeletonBodyHeight),
            shape = TaffyTheme.shapes.card,
        )
    }
}

/** The tags screen SCR-415's semantics tests name. */
const val PROVIDER_CONFIG_BODY_TEST_TAG: String = "providers_config_body"
const val PROVIDER_CONFIG_GONE_TEST_TAG: String = "providers_config_gone"
const val PROVIDER_CONFIG_LOADING_TEST_TAG: String = "providers_config_loading"
const val PROVIDER_SIGN_OUT_SHEET_TEST_TAG: String = "providers_sign_out_sheet"
const val PROVIDER_SIGN_OUT_CONFIRM_TEST_TAG: String = "providers_sign_out_confirm"

private val GlyphSize = 24.dp
private val SkeletonHeaderHeight = 72.dp
private val SkeletonBodyHeight = 180.dp
