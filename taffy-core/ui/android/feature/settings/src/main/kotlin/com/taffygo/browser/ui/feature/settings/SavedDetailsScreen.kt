// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-414 — saved details. */
@Composable
fun SavedDetailsScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: SavedDetailsViewModel = screenViewModel(TaffyDestination.SavedDetails)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    SavedDetailsContent(
        state = state,
        onIntent = viewModel::onIntent,
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun SavedDetailsContent(
    state: SavedDetailsUiState,
    onIntent: (SavedDetailsIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    val hasPeople = state.availability == YouSurfaceAvailability.READY && state.people.isNotEmpty()
    TaffyLazyScreen(
        destination = TaffyDestination.SavedDetails,
        title = taffyString(R.string.taffy_settings_saved_details_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = if (hasPeople) {
            Modifier.testTag(DETAILS_LIST_TEST_TAG)
        } else {
            Modifier
        },
        footer = if (state.availability == YouSurfaceAvailability.READY) {
            {
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_details_add),
                    onClick = { onIntent(SavedDetailsIntent.Add) },
                    modifier = Modifier.fillMaxWidth(),
                    testTag = DETAILS_ADD_TEST_TAG,
                )
            }
        } else {
            null
        },
    ) {
        item(key = "details-promise", contentType = "promise") {
            Text(
                text = taffyString(R.string.taffy_saved_never_sends),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(DETAILS_PROMISE_TEST_TAG),
            )
        }
        when (state.availability) {
            YouSurfaceAvailability.LOADING -> item(
                key = "details-loading",
                contentType = "status",
            ) {
                SavedDetailsLoading()
            }
            YouSurfaceAvailability.UNAVAILABLE -> item(
                key = "details-unavailable",
                contentType = "status",
            ) {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_details_unavailable_title),
                    body = taffyString(R.string.taffy_details_unavailable_body),
                    leading = { SavedDetailsGlyph() },
                )
            }
            YouSurfaceAvailability.READY -> savedDetailsReady(state, onIntent)
        }
    }
    if (state.editor != null) {
        SavedDetailsEditorSheet(state = state, onIntent = onIntent)
    }
}

private fun LazyListScope.savedDetailsReady(
    state: SavedDetailsUiState,
    onIntent: (SavedDetailsIntent) -> Unit,
) {
    if (state.people.isEmpty()) {
        item(key = "details-empty", contentType = "status") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_details_empty_title),
                body = taffyString(R.string.taffy_details_empty_body),
                leading = { SavedDetailsGlyph() },
                action = {
                    TaffyPrimaryButton(
                        label = taffyString(R.string.taffy_details_add),
                        onClick = { onIntent(SavedDetailsIntent.Add) },
                        testTag = DETAILS_EMPTY_ADD_TEST_TAG,
                    )
                },
            )
        }
        return
    }
    items(
        items = state.people,
        key = SavedDetailsRepository.Person::id,
        contentType = { "person" },
    ) { person ->
        TaffyGroupedCard {
            val name = listOf(person.givenName, person.familyName)
                .filter { it.isNotBlank() }
                .joinToString(" ")
                .ifBlank { person.email.ifBlank { person.phone } }
            val supporting = listOf(person.email, person.phone)
                .filter { it.isNotBlank() }
                .joinToString(" · ")
            val description = taffyString(
                R.string.taffy_details_row_description,
                name,
                supporting.ifBlank { name },
            )
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = PersonRowMinHeight)
                    .padding(TaffyTheme.spacing.screenMargin)
                    .testTag("$DETAILS_ROW_TEST_TAG_PREFIX${person.id}")
                    .semantics(mergeDescendants = true) {
                        contentDescription = description
                        heading()
                    },
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                )
                if (supporting.isNotBlank()) {
                    Text(
                        text = supporting,
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                    )
                }
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_details_edit),
                    onClick = { onIntent(SavedDetailsIntent.Edit(person.id)) },
                    testTag = "$DETAILS_EDIT_TEST_TAG_PREFIX${person.id}",
                )
            }
        }
    }
}

@Composable
private fun SavedDetailsLoading() {
    TaffyGroupedCard {
        repeat(2) { index ->
            TaffySkeleton(
                modifier = Modifier
                    .padding(TaffyTheme.spacing.screenMargin)
                    .fillMaxWidth()
                    .height(48.dp),
                accessibleDescription = if (index == 0) {
                    taffyString(R.string.taffy_details_loading)
                } else {
                    null
                },
            )
            if (index == 0) TaffyGroupedCardDivider()
        }
    }
}

@Composable
private fun SavedDetailsGlyph() {
    Icon(
        imageVector = TaffyIcon.IdentificationCard,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
        modifier = Modifier.size(20.dp),
    )
}

const val DETAILS_PROMISE_TEST_TAG: String = "saved_details_promise"
const val DETAILS_ADD_TEST_TAG: String = "saved_details_add"
const val DETAILS_EMPTY_ADD_TEST_TAG: String = "saved_details_empty_add"
const val DETAILS_LIST_TEST_TAG: String = "saved_details_list"
const val DETAILS_ROW_TEST_TAG_PREFIX: String = "saved_details_row_"
const val DETAILS_EDIT_TEST_TAG_PREFIX: String = "saved_details_edit_"

private val PersonRowMinHeight = 72.dp
