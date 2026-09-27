// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * The three skins an action button wears (`handoff/TgButton.dc.html`).
 *
 * Primary is ink on paper and paper on ink — symmetric, and the only fill an
 * action ever gets. Amber is never an action. Danger is a tinted wash, never a
 * solid, so a destructive tap can never be taken by muscle memory.
 */
enum class TaffyButtonVariant {
    /** The one action a screen most wants the user to take. */
    PRIMARY,

    /** An action that is available but is not the point of the screen. */
    SECONDARY,

    /** An action that destroys or refuses something. */
    DANGER,
}
