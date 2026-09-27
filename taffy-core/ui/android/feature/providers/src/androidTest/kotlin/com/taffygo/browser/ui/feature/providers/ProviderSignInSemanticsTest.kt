// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.providerauth.ProviderSignInFailure
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-416 drawn from fixture state, one stage at a time.
 *
 * This is the suite the surface it replaces never had. That row could only be
 * reached by signing in to a real vendor with a real plan, so the state after
 * the code appeared — no link, no copy, no way out — was never looked at by
 * anybody.
 */
class ProviderSignInSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<ProviderSignInIntent>()

    private fun setContent(state: ProviderSignInUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ProviderSignInContent(state = state, onIntent = { intents += it })
            }
        }
    }

    @Test
    fun thePageIsASkeletonUntilTheRosterArrives() {
        setContent(ProviderSignInUiState())

        compose.onNodeWithTag(PROVIDER_SIGN_IN_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_BODY_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aVendorThisBuildCannotSignInToIsExplained() {
        setContent(
            stage(ProviderSignInStage.Idle)
                .copy(status = ProviderSignInUiState.Status.NOT_BUILT),
        )

        compose.onNodeWithTag(PROVIDER_SIGN_IN_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_START_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theCodePanelCarriesAnAddressToOpenACodeToCopyAndAWayOut() {
        setContent(stage(codeReady()))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_VALUE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_OPEN_TEST_TAG)
            .assertHasClickAction()
            .performClick()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_COPY_TEST_TAG)
            .assertHasClickAction()
            .performClick()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CANCEL_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(
            listOf(
                ProviderSignInIntent.OpenVerificationPage,
                ProviderSignInIntent.CopyUserCode,
                ProviderSignInIntent.Cancel,
            ),
            intents,
        )
    }

    @Test
    fun theCountdownIsShownBesideTheNoteThatSaysWhoseClockItIs() {
        setContent(stage(codeReady()))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_COUNTDOWN_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_signin_window_note),
        ).assertExists()
    }

    @Test
    fun theRedirectWaitCarriesAFieldAndSubmitsWhatWasPasted() {
        setContent(stage(ProviderSignInStage.Waiting(ManualCodeEntry(draft = "abc#def"))))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_FIELD_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_REJECTED_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_SUBMIT_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(ProviderSignInIntent.SubmitManualCode), intents)
    }

    @Test
    fun aRefusedCodeIsSaidOnTheFieldAndTheWayOutStays() {
        setContent(
            stage(
                ProviderSignInStage.Waiting(
                    ManualCodeEntry(draft = "half", rejected = true, submitting = false),
                ),
            ),
        )

        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_REJECTED_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_signin_code_entry_rejected),
        ).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CANCEL_TEST_TAG).assertExists()
    }

    @Test
    fun aDeviceCodeWaitDrawsNoField() {
        setContent(stage(ProviderSignInStage.Waiting(null)))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_WAITING_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CODE_ENTRY_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun everyRunningStageOffersAWayOut() {
        val stages = listOf(
            ProviderSignInStage.Starting,
            ProviderSignInStage.Waiting(null),
            ProviderSignInStage.Waiting(ManualCodeEntry()),
            ProviderSignInStage.Exchanging,
        )
        val state = mutableStateOf(stage(stages.first()))
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ProviderSignInContent(state = state.value, onIntent = { intents += it })
            }
        }

        stages.forEach { running ->
            intents.clear()
            compose.runOnIdle {
                state.value = stage(running)
            }
            compose.onNodeWithTag(PROVIDER_SIGN_IN_CANCEL_TEST_TAG).assertExists()
        }
    }

    @Test
    fun leavingSaysWhatActuallyHappenedAndOffersToStartAgain() {
        setContent(stage(ProviderSignInStage.Cancelled))

        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_signin_cancelled_body),
        ).assertExists()
        compose.onNodeWithTag("${PROVIDER_SIGN_IN_OUTCOME_ACTION_PREFIX}$CANCELLED_TAG").performClick()

        assertEquals(listOf(ProviderSignInIntent.Start), intents)
    }

    @Test
    fun aFailureNamesItselfAndOffersToTryAgain() {
        setContent(stage(ProviderSignInStage.Failed(ProviderSignInFailure.DENIED)))

        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_signin_failed_denied),
        ).assertExists()
        compose.onNodeWithTag("${PROVIDER_SIGN_IN_OUTCOME_ACTION_PREFIX}$FAILED_TAG").performClick()

        assertEquals(listOf(ProviderSignInIntent.DismissFailure), intents)
    }

    @Test
    fun aFinishedSignInLeadsOnToTheProvidersOwnPage() {
        setContent(stage(ProviderSignInStage.Succeeded).copy(connected = true))

        compose.onNodeWithTag("${PROVIDER_SIGN_IN_OUTCOME_ACTION_PREFIX}$DONE_TAG").performClick()

        assertEquals(listOf(ProviderSignInIntent.OpenProviderPage), intents)
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

    private companion object {
        const val CANCELLED_TAG = PROVIDER_SIGN_IN_CANCELLED_TEST_TAG
        const val FAILED_TAG = PROVIDER_SIGN_IN_FAILED_TEST_TAG
        const val DONE_TAG = PROVIDER_SIGN_IN_DONE_TEST_TAG
    }
}
