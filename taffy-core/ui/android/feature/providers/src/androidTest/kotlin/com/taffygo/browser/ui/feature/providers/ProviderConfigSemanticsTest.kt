// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.ProviderPresentation
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-415 drawn from fixture state.
 *
 * Every case is one the page decides by itself — which ways in are on offer,
 * whether the key form is even there, whether an action can be pressed — so
 * none of them needs a provider, a credential or a network.
 */
class ProviderConfigSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<ProviderConfigIntent>()

    private fun setContent(state: ProviderConfigUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ProviderConfigContent(state = state, onIntent = { intents += it })
            }
        }
    }

    @Test
    fun thePageIsASkeletonUntilTheRosterArrives() {
        setContent(ProviderConfigUiState())

        compose.onNodeWithTag(PROVIDER_CONFIG_LOADING_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_CONFIG_BODY_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aProviderTheCatalogDroppedSaysSoRatherThanWaiting() {
        setContent(
            ProviderConfigUiState(
                status = ProviderConfigUiState.Status.UNKNOWN,
                providerId = "gone",
            ),
        )

        compose.onNodeWithTag(PROVIDER_CONFIG_GONE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_CONFIG_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aBlockedProviderExplainsItselfAndOffersNothing() {
        setContent(ready().copy(blocked = ProviderRowOffer.Reason.HELD_SHUT, keyForm = null))

        compose.onNodeWithTag(PROVIDER_CONFIG_BLOCKED_TEST_TAG).assertExists()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_config_block_held_shut),
        ).assertExists()
        compose.onNodeWithTag(PROVIDER_KEY_SECTION_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_DEFAULT_SECTION_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun bothWaysInAreSeparatedByTheRuleAndTheSignInIsAbove() {
        setContent(ready().copy(signIn = ProviderSignInOffer.OFFERED))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_BLOCK_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_KEY_DIVIDER_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_KEY_SECTION_TEST_TAG).assertExists()
    }

    @Test
    fun oneWayInDrawsNoRule() {
        setContent(ready())

        compose.onNodeWithTag(PROVIDER_KEY_DIVIDER_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_BLOCK_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aSignInThisBuildCannotRunIsExplainedAndNeverDegradedIntoAKeyForm() {
        setContent(ready().copy(signIn = ProviderSignInOffer.NOT_BUILT, keyForm = null))

        compose.onNodeWithTag(PROVIDER_SIGN_IN_UNAVAILABLE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_IN_CTA_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_KEY_SECTION_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun anEmptyDraftCannotBeSentAndAPlausibleOneCan() {
        val state = mutableStateOf(ready())
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                ProviderConfigContent(state = state.value, onIntent = { intents += it })
            }
        }
        compose.onNodeWithTag(PROVIDER_KEY_SAVE_TEST_TAG).assertIsNotEnabled()

        compose.runOnIdle {
            state.value = ready().copy(
                keyForm = form().copy(
                    draft = "ex-live",
                    verdict = ProviderKeyForm.Verdict.PLAUSIBLE,
                ),
            )
        }

        compose.onNodeWithTag(PROVIDER_KEY_SAVE_TEST_TAG).assertIsEnabled()
    }

    @Test
    fun aDefinitiveRefusalNamesItselfAndNeverOffersToSaveAnyway() {
        setContent(
            ready().copy(
                keyForm = form().copy(
                    draft = "ex-wrong",
                    verdict = ProviderKeyForm.Verdict.PLAUSIBLE,
                    problem = ProviderKeyProblem.KEY_REFUSED,
                ),
            ),
        )

        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_key_problem_refused),
        ).assertExists()
        compose.onNodeWithTag(PROVIDER_KEY_SAVE_ANYWAY_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun anIndefiniteVerdictOffersTheSecondRoad() {
        setContent(
            ready().copy(
                keyForm = form().copy(
                    draft = "ex-unheard",
                    verdict = ProviderKeyForm.Verdict.PLAUSIBLE,
                    problem = ProviderKeyProblem.UNREACHABLE,
                ),
            ),
        )

        compose.onNodeWithTag(PROVIDER_KEY_SAVE_ANYWAY_TEST_TAG)
            .assertExists()
            .performClick()

        assertEquals(listOf(ProviderConfigIntent.SaveKeyAnyway), intents)
    }

    @Test
    fun aCatalogThatNamedNoPagesDrawsNoLinks() {
        setContent(ready().copy(presentation = null))

        compose.onNodeWithTag(PROVIDER_KEY_GET_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_KEY_DOCS_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theStoredCredentialCardCarriesItsThreeActions() {
        setContent(ready().copy(managed = managed()))

        compose.onNodeWithTag(PROVIDER_MANAGED_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_CHANGE_MODEL_TEST_TAG).performClick()
        compose.onNodeWithTag(PROVIDER_REAUTH_TEST_TAG).performClick()
        compose.onNodeWithTag(PROVIDER_SIGN_OUT_TEST_TAG).performClick()

        assertEquals(
            listOf(
                ProviderConfigIntent.ChangeModel,
                ProviderConfigIntent.StartSignIn,
                ProviderConfigIntent.AskSignOut,
            ),
            intents,
        )
    }

    @Test
    fun aPastedKeyWithNoCompiledFlowIsNotOfferedASignInAgain() {
        setContent(ready().copy(managed = managed().copy(canReauthenticate = false)))

        compose.onNodeWithTag(PROVIDER_REAUTH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(PROVIDER_SIGN_OUT_TEST_TAG).assertExists()
    }

    @Test
    fun removingACredentialTakesTwoDeliberateSteps() {
        setContent(ready().copy(managed = managed(), confirmingSignOut = true))

        compose.onNodeWithTag(PROVIDER_SIGN_OUT_SHEET_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_SIGN_OUT_CONFIRM_TEST_TAG).performClick()

        assertEquals(listOf(ProviderConfigIntent.ConfirmSignOut), intents)
    }

    @Test
    fun theDefaultIsAControlOnlyWhereTheProductCouldHonourIt() {
        setContent(ready().copy(managed = managed(), defaultChoice = ProviderDefaultChoice.SHARED))

        compose.onNodeWithTag(PROVIDER_DEFAULT_SECTION_TEST_TAG).assertExists()
        compose.onNodeWithTag(PROVIDER_DEFAULT_CTA_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithText(
            context.getString(R.string.taffy_providers_default_shared),
        ).assertExists()
    }

    private fun ready(): ProviderConfigUiState = ProviderConfigUiState(
        status = ProviderConfigUiState.Status.READY,
        providerId = "example-key-provider",
        displayName = "Example Models",
        keyForm = form(),
        presentation = ProviderPresentation(
            keyPrefix = "ex-",
            getKeyUrl = "https://example.test/keys",
            docsUrl = "https://example.test/docs",
        ),
    )

    private fun form(): ProviderKeyForm = ProviderKeyForm(
        draft = "",
        revealed = false,
        verdict = ProviderKeyForm.Verdict.EMPTY,
        stage = ProviderKeyForm.Stage.IDLE,
        problem = null,
        replacing = false,
    )

    private fun managed(): ManagedCredential = ManagedCredential(
        accountLabel = "ada@example.test",
        planLabel = "Example Pro",
        subscriptionBacked = true,
        confirmed = true,
        modelName = "Example Large",
        canReauthenticate = true,
    )
}
