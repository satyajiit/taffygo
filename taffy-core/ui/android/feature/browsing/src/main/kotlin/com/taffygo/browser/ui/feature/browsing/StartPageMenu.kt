// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The plus's menu: what a request can be made out of, and the shape it can take.
 *
 * It is not the browser's overflow with a different anchor, and the difference
 * is the whole rule: this control belongs to the box it sits on, so what is
 * behind it is about the question. The person's own stores — History,
 * Bookmarks, Open tabs — are attached to the request from here, whole, and
 * reach Taffy as tools it may search (decision 0133); Library, whose records
 * already reach Taffy as sources, is the one tile that still opens a screen.
 * Then the shapes a job can take. Somewhere to *go* does not belong here,
 * which is why You and Settings were taken out; the dock one line below is
 * where the browser's own destinations live.
 *
 * The tiles and the popup are still the browsing menu's, so the two cannot
 * drift in how a menu looks or behaves. Only what is in it differs, and it
 * differs because the two controls answer different questions.
 *
 * The site group is absent for the reason the action row's is: there is no page
 * here to reload, find in, share or filter.
 */
@Composable
internal fun StartPageMenu(
    actions: StartPageMenuActions,
    attached: Set<TaskAttachedStore>,
    onToggleStore: (TaskAttachedStore) -> Unit,
    onChooseShape: (TaskTemplate) -> Unit,
    onDismiss: () -> Unit,
    onAddPages: (() -> Unit)? = null,
) {
    BrowserMenuPopup(
        groups = startPageMenuGroups(actions, attached, onToggleStore, onChooseShape, onAddPages),
        onDismiss = onDismiss,
        alignment = Alignment.BottomStart,
        rises = true,
    )
}

@Composable
private fun startPageMenuGroups(
    actions: StartPageMenuActions,
    attached: Set<TaskAttachedStore>,
    onToggleStore: (TaskAttachedStore) -> Unit,
    onChooseShape: (TaskTemplate) -> Unit,
    onAddPages: (() -> Unit)?,
): List<BrowserMenuGroup> = listOf(
    // A store's tile is a toggle, and says so in its label once it is on the
    // request: the menu closes on the tap, and the chip under the box is what
    // stays to show it. On the Ask overlay's box the group opens with the
    // open tabs themselves — the pages a question can be about, picked in a
    // sheet because there may be many — and the stores follow.
    BrowserMenuGroup(
        heading = taffyString(R.string.taffy_browser_menu_pages),
        headingTestTag = MENU_PAGES_HEADING_TEST_TAG,
        tiles = listOfNotNull(
            onAddPages?.let { open ->
                BrowserMenuTile(
                    icon = TaffyIcon.Plus,
                    label = taffyString(R.string.taffy_ask_add_pages),
                    onSelect = open,
                    testTag = START_ADD_PAGES_TILE_TEST_TAG,
                )
            },
        ) + TaskAttachedStore.entries.map { store ->
            val name = taffyString(storeLabel(store))
            BrowserMenuTile(
                icon = storeIcon(store),
                label = if (store in attached) {
                    taffyString(R.string.taffy_start_store_attached, name)
                } else {
                    name
                },
                onSelect = { onToggleStore(store) },
                testTag = "$START_STORE_TILE_TEST_TAG_PREFIX${store.label}",
            )
        } + BrowserMenuTile(
            icon = TaffyIcon.Books,
            label = taffyString(R.string.taffy_browser_menu_library),
            onSelect = actions.openLibrary,
            testTag = LIBRARY_TEST_TAG,
        ),
    ),
    // The shapes are the one group here that goes nowhere. Choosing one states
    // what kind of job this is and draws a chip under the box; the words are
    // still the person's to type, and nothing starts until they send.
    BrowserMenuGroup(
        heading = taffyString(R.string.taffy_address_bar_task_templates),
        headingTestTag = START_SHAPES_HEADING_TEST_TAG,
        tiles = TaskTemplate.chips.map { template ->
            BrowserMenuTile(
                icon = TaffyIcon.Sparkle,
                label = taffyString(templateChipLabel(template)),
                onSelect = { onChooseShape(template) },
                testTag = "$START_SHAPE_TILE_TEST_TAG_PREFIX${template.label}",
            )
        },
    ),
)

/** The shapes group's heading. */
const val START_SHAPES_HEADING_TEST_TAG: String = "start_menu_heading_shapes"

/** The Add pages tile, present only on the Ask overlay's box. */
const val START_ADD_PAGES_TILE_TEST_TAG: String = "start_menu_add_pages"

/** One shape tile in the plus's menu, by the shape it states. */
const val START_SHAPE_TILE_TEST_TAG_PREFIX: String = "start_menu_shape_"
