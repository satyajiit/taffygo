// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * What the box says under itself: what the typed words mean, and where else
 * they could go.
 *
 * It lives beside the composer rather than inside screen SCR-103 because the
 * box now stands in two places — that screen, and the centre of the start page
 * — and the reading is the half of it that may never differ between them. UX
 * spec section 5 requires the resolution to be shown before anything
 * consequential runs; a second copy of these rows is a second answer to what
 * "shown" means, and only one of them would be under test.
 */
@Composable
internal fun StartPageResults(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        SavedFlowRepeatPanel(state.savedFlows, onIntent)
        if (state.savedFlows.visible) return@Column
        state.reading?.let { reading ->
            InterpretationLine(
                interpretation = reading,
                onChoose = { onIntent(AddressBarIntent.Choose(reading)) },
                compact = state.conditions.asksInPlace,
            )
        }
        // The consent for a job that starts here, said before the person
        // sends and kept while the send is under way — the row above is the
        // request and these are its terms, so they stand together.
        StartPageStartLines(state = state, onIntent = onIntent)
        state.visibleSuggestions().forEach { suggestion ->
            val title = suggestionTitle(suggestion)
            val supporting = suggestionSupporting(suggestion)
            TaffyListRow(
                title = title,
                supporting = supporting,
                accessibleDescription = taffyString(
                    R.string.taffy_address_bar_suggestion_description,
                    title,
                    supporting,
                ),
                testTag = "${SUGGESTION_TEST_TAG_PREFIX}${suggestion.id}",
                onClick = { onIntent(AddressBarIntent.Choose(suggestion.interpretation)) },
            )
        }
    }
}

/**
 * The reading, named and tappable.
 *
 * It is [AddressBarUiState.reading] rather than the resolver's own answer, so a
 * person who stated a shape from the plus is told what *that* means rather than
 * what their words would have meant without it.
 */
@Composable
private fun InterpretationLine(
    interpretation: AddressBarInterpretation,
    onChoose: () -> Unit,
    compact: Boolean = false,
) {
    val reading = taffyString(readingLabel(interpretation))
    val description = taffyString(
        R.string.taffy_address_bar_interpretation_description,
        reading,
        interpretation.input,
    )
    if (compact) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .clickable(role = Role.Button, onClick = onChoose)
                .testTag(INTERPRETATION_TEST_TAG)
                .semantics { contentDescription = description },
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                TaffyIcon.Sparkle,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(16.dp),
            )
            Text(reading, style = TaffyTheme.typography.label, color = TaffyTheme.colors.textSecondary)
        }
        return
    }
    TaffyListRow(
        title = reading,
        supporting = interpretation.input,
        accessibleDescription = description,
        testTag = INTERPRETATION_TEST_TAG,
        onClick = onChoose,
        modifier = Modifier.semantics { contentDescription = description },
    )
}

/**
 * The suggestions worth a row of their own.
 *
 * The resolver's first suggestion is its own reading, and the reading already
 * stands above these rows as [InterpretationLine]; drawn again it was a row
 * that said the same thing under the same words, and with two more readings
 * titled the same way the person saw their own words three times and what
 * each row would do nowhere. A row that says what the reading says is not a
 * suggestion.
 */
internal fun AddressBarUiState.visibleSuggestions(): List<Suggestion> =
    suggestions.filter { it.interpretation != reading }

/**
 * A row is titled by what choosing it does. A command row says the command,
 * a reading row says the reading, and a page row says the page; the person's
 * own words are the supporting line, beside where the row came from.
 */
@Composable
private fun suggestionTitle(suggestion: Suggestion): String =
    when (val interpretation = suggestion.interpretation) {
        is AddressBarInterpretation.BrowserCommand ->
            taffyString(commandLabel(interpretation.command))
        else -> if (suggestion.source == Suggestion.Source.READING) {
            taffyString(readingLabel(interpretation))
        } else {
            suggestion.title
        }
    }

@Composable
private fun suggestionSupporting(suggestion: Suggestion): String {
    val source = taffyString(suggestionSourceLabel(suggestion))
    val detail = suggestion.supportingText?.takeIf(String::isNotBlank)
        ?: suggestion.title.takeIf { suggestion.source == Suggestion.Source.READING && it.isNotBlank() }
        ?: return source
    return taffyString(R.string.taffy_address_bar_suggestion_supporting, source, detail)
}

/**
 * Where a row's words go, said beside them.
 *
 * A page row names the list it came from. A reading row names where the words
 * would be sent, because every reading row came from the same place and a
 * shared label said nothing: the Search and Ask Taffy rows both read
 * "Suggested reading", and "reading" was this code's word, not a person's.
 */
internal fun suggestionSourceLabel(suggestion: Suggestion): Int = when (suggestion.source) {
    Suggestion.Source.READING -> readingSourceLabel(suggestion.interpretation)
    Suggestion.Source.BROWSER_COMMAND -> R.string.taffy_address_bar_source_command
    Suggestion.Source.OPEN_TAB -> R.string.taffy_address_bar_source_open_tab
    Suggestion.Source.BOOKMARK -> R.string.taffy_address_bar_source_bookmark
    Suggestion.Source.HISTORY -> R.string.taffy_address_bar_source_history
}

private fun readingSourceLabel(interpretation: AddressBarInterpretation): Int =
    when (interpretation) {
        is AddressBarInterpretation.GoTo -> R.string.taffy_address_bar_source_go_to
        is AddressBarInterpretation.Search -> R.string.taffy_address_bar_source_search
        is AddressBarInterpretation.AskTaffy -> R.string.taffy_address_bar_source_ask
        is AddressBarInterpretation.TaskForTaffy -> R.string.taffy_address_bar_source_task
        is AddressBarInterpretation.BrowserCommand -> R.string.taffy_address_bar_source_command
    }

private fun commandLabel(command: AddressBarCommand): Int = when (command) {
    AddressBarCommand.OPEN_HISTORY -> R.string.taffy_address_bar_command_history
    AddressBarCommand.OPEN_BOOKMARKS -> R.string.taffy_address_bar_command_bookmarks
    AddressBarCommand.OPEN_DOWNLOADS -> R.string.taffy_address_bar_command_downloads
    AddressBarCommand.OPEN_SETTINGS -> R.string.taffy_address_bar_command_settings
    AddressBarCommand.OPEN_CLEAR_BROWSING_DATA -> R.string.taffy_address_bar_command_clear_data
}

internal fun readingLabel(interpretation: AddressBarInterpretation) = when (interpretation) {
    is AddressBarInterpretation.GoTo -> R.string.taffy_address_bar_reading_go_to
    is AddressBarInterpretation.Search -> R.string.taffy_address_bar_reading_search
    is AddressBarInterpretation.AskTaffy -> R.string.taffy_address_bar_reading_ask
    is AddressBarInterpretation.TaskForTaffy -> R.string.taffy_address_bar_reading_task
    is AddressBarInterpretation.BrowserCommand -> R.string.taffy_address_bar_reading_command
}

/** The reading the box is showing, which the semantics tests name. */
const val INTERPRETATION_TEST_TAG: String = "address_bar_interpretation"

/** One suggestion row, by its own identifier. */
const val SUGGESTION_TEST_TAG_PREFIX: String = "address_bar_suggestion_"
