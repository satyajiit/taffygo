// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What the check answered, and the one question it can raise.
 *
 * The proposal block is the reason screen SCR-418 is not a form with a button.
 * A runtime identified at the bare origin answers its own discovery path
 * there, and the address the transport would actually use is that origin plus
 * the version segment — so "reached, four models" and "every request fails"
 * are both true at once until somebody settles which address was meant. It is
 * asked rather than assumed, because an address nobody typed can never be
 * answered yes by the register (decision 0096 section 1).
 */
@Composable
internal fun EndpointOutcomeSection(
    state: CustomEndpointUiState,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    val outcome = state.outcome ?: return
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_OUTCOME_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        when (outcome) {
            is CustomEndpointOutcome.Reached -> ReachedCard(outcome)
            is CustomEndpointOutcome.Refused -> Text(
                text = taffyString(problemRes(outcome.problem)),
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_PROBLEM_TEST_TAG),
            )
        }
        val proposal = state.proposal
        if (proposal != null && state.proposalUnanswered) {
            ProposalBlock(state = state, proposal = proposal, onIntent = onIntent)
        } else if (proposal != null) {
            // Answered by keeping the address as typed. The caution stays on
            // the page rather than disappearing with the question, because the
            // thing it warns about has not gone away.
            Text(
                text = taffyString(R.string.taffy_providers_endpoint_kept),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_KEPT_TEST_TAG),
            )
        }
    }
}

/** Reached, and what the server said about itself. */
@Composable
private fun ReachedCard(reached: CustomEndpointOutcome.Reached) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_reached),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.positive,
            modifier = Modifier.testTag(CUSTOM_ENDPOINT_REACHED_TEST_TAG),
        )
        Text(
            text = taffyString(
                R.string.taffy_providers_endpoint_detected,
                taffyString(serverKindRes(reached.server)),
            ),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
        )
        // Zero is an answer and is drawn as one. The address is right and
        // nothing is loaded behind it, which is a fact about the server rather
        // than a failure of the check (decision 0096 section 5).
        Text(
            text = if (reached.carriesModels) {
                taffyPlural(
                    R.plurals.taffy_providers_endpoint_found,
                    reached.modelCount,
                    taffyCount(reached.modelCount),
                )
            } else {
                taffyString(R.string.taffy_providers_endpoint_no_models)
            },
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.testTag(CUSTOM_ENDPOINT_COUNT_TEST_TAG),
        )
        // The count is what the server said and the list is what TaffyGo can
        // hold. Saying only the count would present part of a list as all of
        // it, which is the failure decision 0098 section 4 names and decision
        // 0096 section 5 carries onto this screen.
        if (reached.truncated) {
            Text(
                text = taffyString(
                    R.string.taffy_providers_endpoint_kept_models,
                    taffyCount(reached.models.size),
                    taffyCount(reached.modelCount),
                ),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
                modifier = Modifier.testTag(CUSTOM_ENDPOINT_TRUNCATED_TEST_TAG),
            )
        }
    }
}

/**
 * The corrected address, offered and never applied.
 *
 * Two buttons and no default. Taking the correction and keeping what was typed
 * are both real answers — a person may have a proxy in front of their server
 * that does serve the bare path — so neither is chosen for them, and until one
 * of them is pressed the save stays shut.
 */
@Composable
private fun ProposalBlock(
    state: CustomEndpointUiState,
    proposal: String,
    onIntent: (CustomEndpointIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth().testTag(CUSTOM_ENDPOINT_PROPOSAL_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_proposal_title),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = taffyString(R.string.taffy_providers_endpoint_proposal_body, proposal),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyPrimaryButton(
            label = taffyString(R.string.taffy_providers_endpoint_proposal_take, proposal),
            onClick = { onIntent(CustomEndpointIntent.AcceptProposal) },
            enabled = !state.busy,
            icon = TaffyIcon.Check,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_PROPOSAL_TAKE_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_providers_endpoint_proposal_keep),
            onClick = { onIntent(CustomEndpointIntent.KeepAddress) },
            enabled = !state.busy,
            modifier = Modifier.fillMaxWidth(),
            testTag = CUSTOM_ENDPOINT_PROPOSAL_KEEP_TEST_TAG,
        )
    }
}

private fun serverKindRes(server: CustomEndpointOutcome.ServerKind): Int = when (server) {
    CustomEndpointOutcome.ServerKind.OPENAI_COMPATIBLE ->
        R.string.taffy_providers_endpoint_kind_openai

    CustomEndpointOutcome.ServerKind.OLLAMA -> R.string.taffy_providers_endpoint_preset_ollama
    CustomEndpointOutcome.ServerKind.LM_STUDIO -> R.string.taffy_providers_endpoint_preset_lm_studio
    CustomEndpointOutcome.ServerKind.VLLM -> R.string.taffy_providers_endpoint_preset_vllm
    CustomEndpointOutcome.ServerKind.LLAMA_CPP -> R.string.taffy_providers_endpoint_preset_llama_cpp
}

private fun problemRes(problem: CustomEndpointOutcome.Problem): Int = when (problem) {
    CustomEndpointOutcome.Problem.NOTHING_ANSWERED ->
        R.string.taffy_providers_endpoint_problem_nothing

    CustomEndpointOutcome.Problem.NOT_A_MODEL_SERVER ->
        R.string.taffy_providers_endpoint_problem_not_a_server

    CustomEndpointOutcome.Problem.WANTS_A_CREDENTIAL ->
        R.string.taffy_providers_endpoint_problem_credential

    CustomEndpointOutcome.Problem.NO_ANSWER -> R.string.taffy_providers_endpoint_problem_no_answer
}

/** The tags screen SCR-418's semantics tests name for the check's answer. */
const val CUSTOM_ENDPOINT_OUTCOME_TEST_TAG: String = "providers_endpoint_outcome"
const val CUSTOM_ENDPOINT_REACHED_TEST_TAG: String = "providers_endpoint_reached"
const val CUSTOM_ENDPOINT_COUNT_TEST_TAG: String = "providers_endpoint_count"
const val CUSTOM_ENDPOINT_TRUNCATED_TEST_TAG: String = "providers_endpoint_truncated"
const val CUSTOM_ENDPOINT_PROBLEM_TEST_TAG: String = "providers_endpoint_problem"
const val CUSTOM_ENDPOINT_PROPOSAL_TEST_TAG: String = "providers_endpoint_proposal"
const val CUSTOM_ENDPOINT_PROPOSAL_TAKE_TEST_TAG: String = "providers_endpoint_proposal_take"
const val CUSTOM_ENDPOINT_PROPOSAL_KEEP_TEST_TAG: String = "providers_endpoint_proposal_keep"
const val CUSTOM_ENDPOINT_KEPT_TEST_TAG: String = "providers_endpoint_kept"
