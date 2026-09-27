// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyHeroCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.TaffySectionBar
import com.taffygo.browser.ui.core.ui.TaffyStatTile
import com.taffygo.browser.ui.core.ui.TaffySwitch
import com.taffygo.browser.ui.core.ui.TaffyTwoUp
import com.taffygo.browser.ui.core.ui.taffyCount
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** The weekly picture on screen SCR-206: hero, 2-up stats, master card. */
@Composable
fun FilteringShowcase(
    state: FilteringSettingsUiState,
    onIntent: (FilteringSettingsIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffyHeroCard(
            title = taffyString(
                if (state.enabled) {
                    R.string.taffy_filtering_hero_on
                } else {
                    R.string.taffy_filtering_hero_off
                },
            ),
            eyebrow = taffyString(R.string.taffy_filtering_hero_eyebrow),
            body = taffyString(R.string.taffy_filtering_hero_body),
            testTag = FILTERING_HERO_TEST_TAG,
            illustration = painterResource(R.drawable.taffy_hero_ads),
        )
        TaffySectionBar(
            title = taffyString(R.string.taffy_filtering_week_heading),
            eyebrow = taffyString(R.string.taffy_filtering_counts_eyebrow),
        )
        WeekTiles(state)
        MasterCard(state, onIntent)
        TaffySectionBar(
            title = taffyString(R.string.taffy_filtering_why_heading),
            eyebrow = taffyString(R.string.taffy_filtering_why_eyebrow),
        )
        TaffyInfoTile(testTag = FILTERING_WHY_TEST_TAG) {
            Text(
                text = taffyString(R.string.taffy_filtering_why_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

@Composable
private fun WeekTiles(state: FilteringSettingsUiState) {
    val unknown = taffyString(R.string.taffy_filtering_week_unknown)
    TaffyTwoUp(
        first = {
            TaffyStatTile(
                value = state.blockedThisWeek?.let { taffyCount(it) } ?: unknown,
                label = taffyString(R.string.taffy_filtering_week_heading),
                icon = TaffyIcon.Funnel,
                testTag = FILTERING_WEEK_TEST_TAG,
            )
        },
        second = {
            TaffyStatTile(
                value = state.minimumSitesThisWeek?.let { taffyCount(it) } ?: unknown,
                label = taffyString(R.string.taffy_filtering_sites_heading),
                icon = TaffyIcon.GlobeSimple,
                testTag = FILTERING_SITES_TEST_TAG,
            )
        },
    )
    if (state.blockedThisWeek == null) {
        TaffyInfoTile(testTag = FILTERING_WEEK_UNAVAILABLE_TEST_TAG) {
            Text(
                text = taffyString(R.string.taffy_filtering_week_unavailable),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
    Text(
        text = lifetimeCaption(state.blockedTotal),
        style = TaffyTheme.typography.caption,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier.testTag(FILTERING_TOTAL_TEST_TAG),
    )
}

@Composable
private fun MasterCard(
    state: FilteringSettingsUiState,
    onIntent: (FilteringSettingsIntent) -> Unit,
) {
    val name = taffyString(R.string.taffy_filtering_master_toggle)
    TaffyObjectCard(testTag = FILTERING_TOGGLE_TEST_TAG) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            SettingsGlyph(TaffyIcon.ShieldCheck)
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                Text(
                    text = name,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textPrimary,
                )
                Text(
                    text = taffyString(
                        if (state.enabled) {
                            R.string.taffy_filtering_master_summary
                        } else {
                            R.string.taffy_filtering_master_summary_off
                        },
                    ),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffySwitch(
                checked = state.enabled,
                onCheckedChange = { onIntent(FilteringSettingsIntent.SetEnabled(it)) },
                accessibleName = name,
                testTag = FILTERING_SWITCH_TEST_TAG,
            )
        }
    }
}

@Composable
private fun lifetimeCaption(total: Long): String {
    val quantity = total.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
    return taffyPlural(R.plurals.taffy_filtering_lifetime_caption, quantity, total)
}

/** Why-this-matters copy on screen SCR-206. */
const val FILTERING_WHY_TEST_TAG: String = "filtering_why"

/** The week blocked-count tile. */
const val FILTERING_WEEK_TEST_TAG: String = "filtering_week"

/** The week sites tile. */
const val FILTERING_SITES_TEST_TAG: String = "filtering_sites"

/** The hero on screen SCR-206. */
const val FILTERING_HERO_TEST_TAG: String = "filtering_hero"

/** The honest not-counted note, when the week window is absent. */
const val FILTERING_WEEK_UNAVAILABLE_TEST_TAG: String = "filtering_week_unavailable"
