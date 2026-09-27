// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.Composable
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.model.RosterProviderRefusal
import com.taffygo.browser.ui.core.model.RosterRefusalState
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.ThemePreviews

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun ProviderConfigKeyPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderConfigContent(state = keyOnly(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderConfigBothWaysInPreview() {
    TaffyPreview(darkTheme = true) {
        ProviderConfigContent(state = bothWaysIn(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderConfigConnectedPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderConfigContent(state = connected(), onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun ProviderConfigBlockedPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderConfigContent(
            state = keyOnly().copy(
                blocked = ProviderRowOffer.Reason.HELD_SHUT,
                keyForm = null,
            ),
            onIntent = {},
        )
    }
}

/**
 * A connected credential the vendor is declining to spend right now. The card
 * stays green and keeps every action; the refusal is a line under the model.
 */
@ThemePreviews
@Composable
private fun ProviderConfigRefusedPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderConfigContent(
            state = connected().copy(
                managed = connected().managed?.copy(
                    lastRefusal = RosterRefusalState(
                        refusal = RosterProviderRefusal.RATE_LIMIT,
                        atMonotonicMs = 90_000uL,
                        observedAtEpochMillis = System.currentTimeMillis() - FIVE_MINUTES_MILLIS,
                    ),
                ),
            ),
            onIntent = {},
        )
    }
}

@FontScalePreviews
@Composable
private fun ProviderConfigScaledPreview() {
    TaffyPreview(darkTheme = false) {
        ProviderConfigContent(state = connected(), onIntent = {})
    }
}

/**
 * A provider that takes a key, with a refused draft on the field.
 *
 * The fixture carries a served prefix and a key page, because those are the two
 * catalog facts the form's behaviour depends on and a preview without them
 * shows a form that has quietly lost half its guidance.
 */
private fun keyOnly(): ProviderConfigUiState = ProviderConfigUiState(
    status = ProviderConfigUiState.Status.READY,
    providerId = "example-key-provider",
    displayName = "Example Models",
    keyForm = ProviderKeyForm(
        draft = "zz-not-one-of-theirs",
        revealed = false,
        verdict = ProviderKeyForm.Verdict.PREFIX_MISMATCH,
        stage = ProviderKeyForm.Stage.IDLE,
        problem = ProviderKeyProblem.PREFIX_MISMATCH,
        replacing = false,
    ),
    presentation = ProviderPresentation(
        keyPrefix = "ex-",
        getKeyUrl = "https://example.test/keys",
        docsUrl = "https://example.test/docs",
    ),
)

/** Both ways in, which is the one layout with a rule down the middle of it. */
private fun bothWaysIn(): ProviderConfigUiState = keyOnly().copy(
    displayName = "Example Plans",
    signIn = ProviderSignInOffer.OFFERED,
    keyForm = keyOnly().keyForm?.copy(
        draft = "",
        verdict = ProviderKeyForm.Verdict.EMPTY,
        problem = null,
    ),
)

/** A stored credential, its model, and the default this page can offer. */
private fun connected(): ProviderConfigUiState = bothWaysIn().copy(
    managed = ManagedCredential(
        accountLabel = "ada@example.test",
        planLabel = "Example Pro",
        subscriptionBacked = true,
        confirmed = true,
        modelName = "Example Large",
        canReauthenticate = true,
    ),
    keyForm = bothWaysIn().keyForm?.copy(replacing = true),
    defaultChoice = ProviderDefaultChoice.OFFERED,
)

private const val FIVE_MINUTES_MILLIS = 5L * 60L * 1_000L
