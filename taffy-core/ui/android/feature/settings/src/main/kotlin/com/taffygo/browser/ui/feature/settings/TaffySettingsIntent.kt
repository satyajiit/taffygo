// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-405 can be asked to do. */
sealed interface TaffySettingsIntent {

    /** Open How Taffy talks. */
    data object OpenPersonality : TaffySettingsIntent

    /** Open What Taffy can do. */
    data object OpenSkills : TaffySettingsIntent

    /** Open AI and providers. */
    data object OpenAiProviders : TaffySettingsIntent

    /**
     * Turn suggestions while you type on or off (decision 0097 section 7).
     *
     * Carries no value: the switch shows what is stored, so the intent is
     * "flip what stands" rather than "set it to what this screen thinks it is".
     */
    data object ToggleComposerSuggestions : TaffySettingsIntent
}
