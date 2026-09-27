// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The rule between a plan and a key.
 *
 * Drawn only where a provider offers both. It is what keeps the page to one
 * primary road at a time: above it, the one-tap way in; below it, the errand.
 */
@Composable
internal fun OrPasteAKeyRule() {
    Row(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_KEY_DIVIDER_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Rule(modifier = Modifier.weight(1f))
        Text(
            text = taffyString(R.string.taffy_providers_or_paste_key),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
        Rule(modifier = Modifier.weight(1f))
    }
}

/**
 * The key half of the page: what the catalog said, the field, and one action.
 *
 * The prefix hint sits above the field rather than below it, because it is the
 * thing that stops a wasted round trip: the form can say a pasted value is not
 * one of this vendor's keys before a call is spent proving it.
 */
@Composable
internal fun ProviderKeySection(
    form: ProviderKeyForm,
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    Column(
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_KEY_SECTION_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        KeyLabelRow(state = state, onIntent = onIntent)
        state.presentation?.keyPrefix?.let { prefix ->
            Text(
                text = taffyString(R.string.taffy_providers_key_prefix_hint, prefix),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        KeyField(form = form, onIntent = onIntent, onDone = {
            keyboard?.hide()
            onIntent(ProviderConfigIntent.SaveKey)
        })
        form.problem?.let { problem ->
            Text(
                text = taffyString(problemRes(problem)),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(PROVIDER_KEY_PROBLEM_TEST_TAG),
            )
        }
        TaffyPrimaryButton(
            label = taffyString(actionRes(form)),
            onClick = { onIntent(ProviderConfigIntent.SaveKey) },
            enabled = form.actionable,
            loading = form.busy,
            icon = if (form.stage == ProviderKeyForm.Stage.CONNECTED) TaffyIcon.Check else null,
            modifier = Modifier.fillMaxWidth(),
            testTag = PROVIDER_KEY_SAVE_TEST_TAG,
        )
        if (form.offersSaveAnyway) {
            // The offer an indefinite verdict earns (decision 0083): the
            // provider was not definitively heard, and an unreachable endpoint
            // is not a wrong key. A definitive refusal never renders this.
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_key_save_anyway),
                onClick = { onIntent(ProviderConfigIntent.SaveKeyAnyway) },
                modifier = Modifier.fillMaxWidth(),
                testTag = PROVIDER_KEY_SAVE_ANYWAY_TEST_TAG,
            )
        }
        SecurityNote()
        state.presentation?.docsUrl?.let {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_key_docs),
                onClick = { onIntent(ProviderConfigIntent.OpenDocs) },
                icon = TaffyIcon.ArrowUpRight,
                modifier = Modifier.fillMaxWidth(),
                testTag = PROVIDER_KEY_DOCS_TEST_TAG,
            )
        }
    }
}

/** The field's name, and the link to where this vendor issues keys. */
@Composable
private fun KeyLabelRow(
    state: ProviderConfigUiState,
    onIntent: (ProviderConfigIntent) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = taffyString(R.string.taffy_providers_key_label),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.weight(1f),
        )
        // Absent means show nothing. A catalog that named no page is not a
        // catalog naming an empty one, and a link to nowhere is worse than the
        // errand it was meant to shorten.
        if (state.presentation?.getKeyUrl != null) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_providers_key_get),
                onClick = { onIntent(ProviderConfigIntent.OpenKeyPage) },
                icon = TaffyIcon.ArrowUpRight,
                size = TaffyButtonSize.COMPACT,
                testTag = PROVIDER_KEY_GET_TEST_TAG,
            )
        }
    }
}

@Composable
private fun KeyField(
    form: ProviderKeyForm,
    onIntent: (ProviderConfigIntent) -> Unit,
    onDone: () -> Unit,
) {
    OutlinedTextField(
        value = form.draft,
        onValueChange = { onIntent(ProviderConfigIntent.ChangeKey(it)) },
        placeholder = { Text(taffyString(R.string.taffy_providers_key_placeholder)) },
        isError = form.verdict == ProviderKeyForm.Verdict.PREFIX_MISMATCH ||
            form.problem != null,
        enabled = !form.busy,
        singleLine = true,
        visualTransformation = if (form.revealed) {
            VisualTransformation.None
        } else {
            PasswordVisualTransformation()
        },
        keyboardOptions = KeyboardOptions(
            keyboardType = KeyboardType.Password,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = { onDone() }),
        shape = TaffyTheme.shapes.card,
        // A word rather than a glyph. The palette carries one eye — the
        // struck-through one — so an icon pair here would have had to invent
        // the other half, and "Show"/"Hide" needs no legend anyway.
        trailingIcon = {
            TaffySecondaryButton(
                label = taffyString(
                    if (form.revealed) {
                        R.string.taffy_providers_key_hide
                    } else {
                        R.string.taffy_providers_key_show
                    },
                ),
                onClick = { onIntent(ProviderConfigIntent.ToggleKeyVisible) },
                size = TaffyButtonSize.COMPACT,
                modifier = Modifier.padding(end = TaffyTheme.spacing.step),
                testTag = PROVIDER_KEY_REVEAL_TEST_TAG,
            )
        },
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = TaffyTheme.colors.surfaceRaised,
            unfocusedContainerColor = TaffyTheme.colors.surfaceRaised,
            // The border is the verdict: neutral until there is something to
            // judge, the danger tone when the value is not this vendor's shape,
            // and the positive tone when it is worth spending a call on. It
            // never says the key works — only the provider can say that.
            focusedBorderColor = borderFor(form),
            unfocusedBorderColor = borderFor(form),
            errorBorderColor = TaffyTheme.colors.danger,
        ),
        modifier = Modifier.fillMaxWidth().testTag(PROVIDER_KEY_FIELD_TEST_TAG),
    )
}

@Composable
private fun SecurityNote() {
    Row(
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(
            imageVector = TaffyIcon.LockSimple,
            contentDescription = null,
            tint = TaffyTheme.colors.textSecondary,
            modifier = Modifier.size(NoteGlyphSize),
        )
        Text(
            text = taffyString(R.string.taffy_providers_key_security),
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun Rule(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .height(RuleThickness)
            .background(TaffyTheme.colors.outline),
    )
}

@Composable
private fun borderFor(form: ProviderKeyForm) = when (form.verdict) {
    ProviderKeyForm.Verdict.EMPTY -> TaffyTheme.colors.outline
    ProviderKeyForm.Verdict.PREFIX_MISMATCH -> TaffyTheme.colors.danger
    ProviderKeyForm.Verdict.PLAUSIBLE -> TaffyTheme.colors.positive
}

private fun actionRes(form: ProviderKeyForm): Int = when (form.stage) {
    ProviderKeyForm.Stage.TESTING -> R.string.taffy_providers_key_testing
    ProviderKeyForm.Stage.CONNECTED -> R.string.taffy_providers_key_connected
    ProviderKeyForm.Stage.IDLE ->
        if (form.replacing) {
            R.string.taffy_providers_key_replace
        } else {
            R.string.taffy_providers_key_save
        }
}

private fun problemRes(problem: ProviderKeyProblem): Int = when (problem) {
    ProviderKeyProblem.EMPTY -> R.string.taffy_providers_key_problem_empty
    ProviderKeyProblem.PREFIX_MISMATCH -> R.string.taffy_providers_key_problem_prefix
    ProviderKeyProblem.STORE_FAILED -> R.string.taffy_providers_key_problem_store
    ProviderKeyProblem.KEY_REFUSED -> R.string.taffy_providers_key_problem_refused
    ProviderKeyProblem.BILLING_REFUSED -> R.string.taffy_providers_key_problem_billing
    ProviderKeyProblem.MODEL_NOT_FOUND -> R.string.taffy_providers_key_problem_model
    ProviderKeyProblem.RATE_LIMITED -> R.string.taffy_providers_key_problem_rate_limited
    ProviderKeyProblem.OVERLOADED -> R.string.taffy_providers_key_problem_overloaded
    ProviderKeyProblem.TIMED_OUT -> R.string.taffy_providers_key_problem_timed_out
    ProviderKeyProblem.UNREACHABLE -> R.string.taffy_providers_key_problem_unreachable
    ProviderKeyProblem.UNSETTLED -> R.string.taffy_providers_key_problem_unsettled
    ProviderKeyProblem.TEST_UNAVAILABLE -> R.string.taffy_providers_key_problem_unavailable
    ProviderKeyProblem.NO_MODEL_LISTED -> R.string.taffy_providers_key_problem_no_model_listed
}

/** The tags screen SCR-415's key form names in its semantics tests. */
const val PROVIDER_KEY_SECTION_TEST_TAG: String = "providers_key_section"
const val PROVIDER_KEY_FIELD_TEST_TAG: String = "providers_key_field"
const val PROVIDER_KEY_REVEAL_TEST_TAG: String = "providers_key_reveal"
const val PROVIDER_KEY_SAVE_TEST_TAG: String = "providers_key_save"
const val PROVIDER_KEY_SAVE_ANYWAY_TEST_TAG: String = "providers_key_save_anyway"
const val PROVIDER_KEY_PROBLEM_TEST_TAG: String = "providers_key_problem"
const val PROVIDER_KEY_DIVIDER_TEST_TAG: String = "providers_key_divider"
const val PROVIDER_KEY_GET_TEST_TAG: String = "providers_key_get"
const val PROVIDER_KEY_DOCS_TEST_TAG: String = "providers_key_docs"

private val NoteGlyphSize = 16.dp
private val RuleThickness = 1.dp
