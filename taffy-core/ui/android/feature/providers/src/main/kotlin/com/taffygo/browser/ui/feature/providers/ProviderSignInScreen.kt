// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.PersistableBundle
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
import androidx.compose.ui.platform.LocalContext
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
 * Screen SCR-416 — one vendor's sign-in, from the button to the credential.
 *
 * It replaces a row that rendered the verification address and the user code as
 * plain, unpressable text: no link, no way to copy, no way to stop, and nothing
 * moving until a deadline nobody could see expired. Every one of those is a
 * control here. The exact browser flow can be cancelled; the vendor's own
 * expiry still is not exposed to Android, so the visible timer says what it is.
 */
@Composable
fun ProviderSignInScreen(
    destination: TaffyDestination.ProviderSignIn,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: ProviderSignInViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val copyLabel = taffyString(R.string.taffy_providers_signin_code_clip_label)

    LaunchedEffect(Unit) { viewModel.onShown() }

    ProviderSignInContent(
        state = state,
        onIntent = { intent ->
            // The clipboard is a platform surface, so the code reaches it from
            // here rather than from the view model, which holds nothing about
            // it. Marked sensitive so the system's paste preview does not
            // shoulder-surf a one-time code onto the screen.
            if (intent == ProviderSignInIntent.CopyUserCode) {
                val stage = state.stage as? ProviderSignInStage.CodeReady
                stage?.let { copyUserCode(context, copyLabel, it.userCode) }
            } else {
                viewModel.onIntent(intent, navigator)
            }
        },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun ProviderSignInContent(
    state: ProviderSignInUiState,
    onIntent: (ProviderSignInIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.ProviderSignIn(state.providerId),
        title = state.displayName.ifBlank {
            taffyString(R.string.taffy_providers_sign_in_title)
        },
        onBack = onBack,
        modifier = modifier,
    ) {
        when (state.status) {
            ProviderSignInUiState.Status.LOADING -> SignInSkeleton()
            ProviderSignInUiState.Status.UNKNOWN -> SignInProviderGone()
            ProviderSignInUiState.Status.NOT_BUILT ->
                SignInNotBuiltNotice(state.displayName)

            ProviderSignInUiState.Status.NOT_CLEARED ->
                SignInNotClearedNotice(state.displayName)

            ProviderSignInUiState.Status.READY -> Column(
                modifier = Modifier.fillMaxWidth().testTag(PROVIDER_SIGN_IN_BODY_TEST_TAG),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                ProviderSignInHeader(state = state)
                ProviderSignInStagePanel(state = state, onIntent = onIntent)
            }
        }
    }
}

/** The catalog no longer carries this vendor. */
@Composable
private fun SignInProviderGone() {
    TaffyEmptyState(
        title = taffyString(R.string.taffy_providers_config_gone_title),
        body = taffyString(R.string.taffy_providers_config_gone_body),
        leading = {
            Icon(
                imageVector = TaffyIcon.Cloud,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(SignInGlyphSize),
            )
        },
        modifier = Modifier.testTag(PROVIDER_SIGN_IN_GONE_TEST_TAG),
    )
}

@Composable
private fun SignInSkeleton() {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_SIGN_IN_LOADING_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SignInSkeletonHeaderHeight),
            shape = TaffyTheme.shapes.card,
            accessibleDescription = taffyString(R.string.taffy_providers_signin_loading),
        )
        TaffySkeleton(
            modifier = Modifier.fillMaxWidth().height(SignInSkeletonBodyHeight),
            shape = TaffyTheme.shapes.card,
        )
    }
}

/**
 * Put the user code on the clipboard.
 *
 * A device code is a one-time secret in transit: it authorizes an account for
 * as long as the vendor's window lasts, so the clip is flagged sensitive where
 * the platform understands that, and the system stops previewing it.
 */
private fun copyUserCode(context: Context, label: String, code: String) {
    val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return
    val clip = ClipData.newPlainText(label, code)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        clip.description.extras = PersistableBundle().apply {
            putBoolean(ClipDescription.EXTRA_IS_SENSITIVE, true)
        }
    }
    clipboard.setPrimaryClip(clip)
}

/** The tags screen SCR-416's semantics tests name. */
const val PROVIDER_SIGN_IN_BODY_TEST_TAG: String = "providers_sign_in_body"
const val PROVIDER_SIGN_IN_GONE_TEST_TAG: String = "providers_sign_in_gone"
const val PROVIDER_SIGN_IN_LOADING_TEST_TAG: String = "providers_sign_in_loading"

private val SignInGlyphSize = 24.dp
private val SignInSkeletonHeaderHeight = 72.dp
private val SignInSkeletonBodyHeight = 200.dp
