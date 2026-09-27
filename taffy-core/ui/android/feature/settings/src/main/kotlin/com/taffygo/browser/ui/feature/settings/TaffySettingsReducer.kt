// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * Navigation leaves screen SCR-405's own state alone, and so does the switch.
 *
 * The stored preference is what the switch draws from, so flipping it here as
 * well would put a second answer on the screen that the store had not agreed to
 * yet — briefly, and wrongly, if the write does not land.
 */
internal fun reduceTaffySettings(
    state: TaffySettingsUiState,
    intent: TaffySettingsIntent,
): TaffySettingsUiState = when (intent) {
    TaffySettingsIntent.OpenPersonality,
    TaffySettingsIntent.OpenSkills,
    TaffySettingsIntent.OpenAiProviders,
    TaffySettingsIntent.ToggleComposerSuggestions,
    -> state
}
