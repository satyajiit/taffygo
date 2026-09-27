// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFailure
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/**
 * Debug-only Compose previews; never part of the product APK.
 *
 * Every stage is here, which is the point. The surface these replace could only
 * be seen by signing in to a real vendor with a real plan, so its later states
 * were never looked at by anybody — a code panel that had gone dead looked
 * exactly like one that had not.
 */
@ThemePreviews
@Composable
private fun ProviderSignInIdlePreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(state = stage(ProviderSignInStage.Idle), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderSignInCodePreview() {
    TaffyPreview(darkTheme = true) {
        ProviderSignInContent(state = stage(codeReady()), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderSignInWaitingPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(state = stage(ProviderSignInStage.Waiting(null)), onIntent = {})
    }
}

/** A redirect wait with its way back (decision 0095 section 2). */
@ThemePreviews
@Composable
private fun ProviderSignInWaitingWithCodeEntryPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(
            state = stage(ProviderSignInStage.Waiting(ManualCodeEntry())),
            onIntent = {},
        )
    }
}

/** The browser declined what was pasted; the person can try again. */
@ThemePreviews
@Composable
private fun ProviderSignInCodeRejectedPreview() {
    TaffyPreview(darkTheme = true) {
        ProviderSignInContent(
            state = stage(
                ProviderSignInStage.Waiting(
                    ManualCodeEntry(draft = "half-a-co", rejected = true, submitting = false),
                ),
            ),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun ProviderSignInDonePreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(
            state = stage(ProviderSignInStage.Succeeded).copy(connected = true),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun ProviderSignInFailedPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(
            state = stage(ProviderSignInStage.Failed(ProviderSignInFailure.DENIED)),
            onIntent = {},
        )
    }
}

@ThemePreviews
@Composable
private fun ProviderSignInLeftPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(state = stage(ProviderSignInStage.Cancelled), onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun ProviderSignInCodeScaledPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderSignInContent(state = stage(codeReady()), onIntent = {})
    }
}

private fun codeReady(): ProviderSignInStage.CodeReady = ProviderSignInStage.CodeReady(
    verificationUrl = "https://vendor.example.test/device",
    userCode = "BDWN-XKQP",
    remainingSeconds = 521,
)

private fun stage(stage: ProviderSignInStage): ProviderSignInUiState = ProviderSignInUiState(
    status = ProviderSignInUiState.Status.READY,
    providerId = "example-plan-vendor",
    displayName = "Example Plans",
    stage = stage,
)
