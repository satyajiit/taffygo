// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * The seven visual treatments a status chip can wear — the seven task states
 * of the design document, and only those seven.
 *
 * The split that matters most is STOPPED against FAILED: stopped is what the
 * user did, failed is what happened to Taffy, and they never share a
 * treatment. Only WAITING gets the solid accent, because it is the only state
 * that needs a human; only DONE is green.
 */
enum class TaffyStatusStyle {
    /** Accent wash, a bordered pulsing dot: Taffy is working. */
    RUNNING,

    /** Solid accent, the raised hand: Taffy needs a person. */
    WAITING,

    /** An outline chip: held until the user resumes it. */
    PAUSED,

    /** A positive wash: a validated complete result. */
    DONE,

    /** An accent wash, the half-filled circle: a result with labelled gaps. */
    PARTLY_DONE,

    /** A sunken chip, the stop mark: what the user did. */
    STOPPED,

    /** A danger wash, the warning mark: what happened to Taffy. */
    FAILED,
}
