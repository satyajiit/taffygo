// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One card of screen SCR-104's grid.
 *
 * Close sits outside the card's merged body on purpose: the body is one
 * accessible element with one description, and a control folded inside it would
 * either disappear from that element or split it in two.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun TabSwitcherCard(
    card: TabCard,
    isSelecting: Boolean,
    onIntent: (TabSwitcherIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val label = cardLabel(card)
    val description = cardDescription(card, label)
    val closeDescription = taffyString(R.string.taffy_tab_switcher_close_description, label)
    Box(
        modifier = modifier
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(
                width = if (card.openedByTaffy || card.checkedForAsk) {
                    TaffyBorders.emphasis
                } else {
                    TaffyBorders.standard
                },
                color = when {
                    card.openedByTaffy -> TaffyTheme.colors.accent
                    card.checkedForAsk -> TaffyTheme.colors.accent
                    card.isPrivate -> TaffyTheme.colors.privateTint
                    card.isSelected -> TaffyTheme.colors.textPrimary
                    else -> TaffyTheme.colors.outline
                },
                shape = TaffyTheme.shapes.card,
            ),
    ) {
        Column(
            modifier = Modifier
                .combinedClickable(
                    onClick = {
                        onIntent(
                            if (isSelecting) {
                                TabSwitcherIntent.ToggleSelected(card.id)
                            } else {
                                TabSwitcherIntent.Select(card.id)
                            },
                        )
                    },
                    onLongClick = { onIntent(TabSwitcherIntent.LongPress(card.id)) },
                )
                .padding(CardPadding)
                .testTag("$TAB_TEST_TAG_PREFIX${card.id.value}")
                .semantics(mergeDescendants = true) {
                    contentDescription = description
                    selected = card.isSelected
                },
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            CardPreview(card = card)
            CardFooter(card = card, label = label)
        }
        CloseControl(
            description = closeDescription,
            onClick = { onIntent(TabSwitcherIntent.Close(card.id)) },
            testTag = "$CLOSE_TEST_TAG_PREFIX${card.id.value}",
            modifier = Modifier.align(Alignment.TopEnd),
        )
    }
}

/**
 * The page snapshot, or the start page's weather when the tab has been nowhere.
 *
 * A page Taffy is reading gets the skeleton, which is honest — something is
 * genuinely happening to it. A tab that has not been anywhere has no page to
 * snapshot, so the card wears the same wash and mark that tab opens onto,
 * rather than a white hole. Every other missing thumbnail is a slab with no
 * caption: "preview unavailable" claimed a source that was not there.
 */
@Composable
private fun CardPreview(card: TabCard) {
    Box(modifier = Modifier.fillMaxWidth().height(PreviewHeight)) {
        if (card.isBeingRead) {
            TaffySkeleton(
                modifier = Modifier.matchParentSize(),
                shape = PreviewShape,
                accessibleDescription = null,
            )
        } else if (card.hasBeenNowhere) {
            // A blank tab has no page to snapshot. The start page's weather
            // and the mark in its centre are the same face that tab opens
            // onto, so the card is not a white hole in the grid.
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .clip(PreviewShape)
                    .testTag("$PREVIEW_TEST_TAG_PREFIX${card.id.value}"),
                contentAlignment = Alignment.Center,
            ) {
                StartPageBackdrop(modifier = Modifier.matchParentSize())
                TaffyBrandMark(size = EmptyMarkSize, contentDescription = null)
            }
        } else {
            val thumbnail = card.thumbnail
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .clip(PreviewShape)
                    .background(
                        if (card.isPrivate) {
                            TaffyTheme.colors.privateTintWash
                        } else {
                            TaffyTheme.colors.imagePlaceholder
                        },
                    )
                    .testTag("$PREVIEW_TEST_TAG_PREFIX${card.id.value}"),
            ) {
                if (thumbnail != null) {
                    Image(
                        bitmap = thumbnail.asImageBitmap(),
                        contentDescription = null,
                        contentScale = ContentScale.Crop,
                        alignment = Alignment.TopCenter,
                        modifier = Modifier.matchParentSize(),
                    )
                }
            }
        }
        val badge = badgeText(card)
        if (badge != null) {
            Row(
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(BadgeInset)
                    .clip(TaffyTheme.shapes.pill)
                    .background(TaffyTheme.colors.accent)
                    .padding(horizontal = TaffyTheme.spacing.tight, vertical = BadgePadding),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(
                    imageVector = TaffyIcon.Sparkle,
                    contentDescription = null,
                    modifier = Modifier.size(BadgeGlyphSize),
                    tint = TaffyTheme.colors.accentOn,
                )
                Text(
                    text = badge,
                    style = TaffyTheme.typography.label,
                    color = TaffyTheme.colors.accentOn,
                    maxLines = 1,
                )
            }
        }
        val age = openAgeLabel(card).takeIf { TabOpenAge.mayShowOn(card) }
        if (age != null) {
            Text(
                text = age,
                style = TaffyTheme.typography.micro,
                color = TaffyTheme.colors.textSecondary,
                maxLines = 1,
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(BadgeInset)
                    .clip(TaffyTheme.shapes.pill)
                    .background(TaffyTheme.colors.surfaceRaised)
                    .padding(horizontal = TaffyTheme.spacing.tight, vertical = BadgePadding)
                    .testTag("$AGE_TEST_TAG_PREFIX${card.id.value}"),
            )
        }
    }
}

@Composable
private fun CardFooter(card: TabCard, label: String) {
    val title = card.title.ifBlank { label }
    val supporting = card.host.takeIf { it.isNotBlank() && it != title }
    val nowhere = card.hasBeenNowhere
    // The mark belongs to the title, so it is centred on the title's own line
    // box. `Alignment.Top` set a fourteen-unit mark against a twenty-one-unit
    // line and left it riding three and a half units high against the word
    // beside it — and further at every larger text size, because the line is
    // scalable and the mark is not. Centring the row instead would drop it
    // between the two lines the moment a card has a host as well as a title.
    // The line height is read from the type scale rather than written down
    // here, so the mark stays on the line at 200% text.
    val titleLine = with(LocalDensity.current) {
        TaffyTheme.typography.title.lineHeight.toDp()
    }
    Row(
        modifier = Modifier.fillMaxWidth().heightIn(min = FooterMinimumHeight),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.Top,
    ) {
        Box(modifier = Modifier.height(titleLine), contentAlignment = Alignment.Center) {
            Box(
                modifier = Modifier
                    .size(FaviconSize)
                    .clip(FaviconShape)
                    .background(TaffyTheme.colors.imagePlaceholder)
                    .testTag("$FAVICON_TEST_TAG_PREFIX${card.id.value}"),
                contentAlignment = Alignment.Center,
            ) {
                val favicon = card.favicon
                when {
                    nowhere -> TaffyBrandMark(size = FaviconSize, contentDescription = null)
                    favicon != null -> Image(
                        bitmap = favicon.asImageBitmap(),
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.size(FaviconSize),
                    )
                    else -> SiteLetterAvatar(
                        host = card.host,
                        modifier = Modifier.size(FaviconSize),
                        letterStyle = TaffyTheme.typography.micro,
                    )
                }
            }
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (supporting != null) {
                Text(
                    text = supporting,
                    style = TaffyTheme.typography.caption,
                    color = TaffyTheme.colors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun CloseControl(
    description: String,
    onClick: () -> Unit,
    testTag: String,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .size(TaffyTheme.spacing.minimumTouchTarget)
            // Clipped before it is clickable, exactly as [BrowserChromeButton]
            // is. Without this the press drew a square of ink over the whole
            // forty-eight-unit target while the thing under the finger was a
            // twenty-six-unit round well — about three times the area, and a
            // corner of it over the card behind. `pill` is fifty percent,
            // which on a square is a circle.
            .clip(TaffyTheme.shapes.pill)
            .clickable(role = Role.Button, onClick = onClick)
            .testTag(testTag)
            .semantics(mergeDescendants = true) { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .size(CloseWellSize)
                .clip(TaffyTheme.shapes.pill)
                .background(TaffyTheme.colors.surfaceRaised),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = TaffyIcon.X,
                contentDescription = null,
                modifier = Modifier.size(CloseGlyphSize),
                tint = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun badgeText(card: TabCard): String? = when {
    card.isBeingRead -> taffyString(R.string.taffy_tab_switcher_reading)
    card.factCount > 0 -> taffyPlural(R.plurals.taffy_tab_switcher_facts, card.factCount, card.factCount)
    else -> null
}

@Composable
private fun cardLabel(card: TabCard): String =
    tabCardLabel(card, taffyString(R.string.taffy_tab_switcher_blank_tab))

@Composable
private fun cardDescription(card: TabCard, label: String): String {
    val joinTemplate = taffyString(R.string.taffy_tab_switcher_description_join)
    val locale = LocalConfiguration.current.locales[0]
    return tabCardDescription(
        card = card,
        label = label,
        showingNow = taffyString(R.string.taffy_tab_switcher_showing_now),
        openedByTaffy = taffyString(R.string.taffy_tab_switcher_by_taffy),
        badge = badgeText(card),
        join = { left, right -> String.format(locale, joinTemplate, left, right) },
        chosenForAsk = taffyString(R.string.taffy_tab_switcher_chosen_for_ask),
    )
}

@Composable
private fun openAgeLabel(card: TabCard): String? {
    val now = remember { System.currentTimeMillis() }
    val age = TabOpenAge.of(card.openedAtEpochMillis, now) ?: return null
    val minutes = age.minutes.toInt()
    val hours = age.hours.toInt()
    val days = age.days.toInt()
    return when {
        age.justOpened -> taffyString(R.string.taffy_tab_switcher_open_age_just)
        age.underAnHour -> taffyPlural(R.plurals.taffy_tab_switcher_open_age_minutes, minutes, minutes)
        age.underADay -> taffyPlural(R.plurals.taffy_tab_switcher_open_age_hours, hours, hours)
        else -> taffyPlural(R.plurals.taffy_tab_switcher_open_age_days, days, days)
    }
}

private val CardPadding = 7.dp
private val PreviewHeight = 96.dp
private val PreviewShape = RoundedCornerShape(11.dp)
private val BadgeInset = 7.dp
private val BadgePadding = 3.dp
private val BadgeGlyphSize = 11.dp
private val EmptyMarkSize = 40.dp
private val FaviconSize = 14.dp
private val FaviconShape = RoundedCornerShape(4.dp)
private val FooterMinimumHeight = 44.dp
private val CloseWellSize = 26.dp
private val CloseGlyphSize = 13.dp
