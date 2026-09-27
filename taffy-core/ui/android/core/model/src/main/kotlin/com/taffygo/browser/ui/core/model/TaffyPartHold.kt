// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Why the last attempt at one part did not finish (screen SCR-203).
 *
 * Every value is a verdict the browser reached, never one this layer worked
 * out. The screen turns it into a sentence; it never turns the absence of one
 * into a guess.
 */
enum class TaffyPartHold(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /** Whether asking again could finish, with nothing else changing. */
    val canRetry: Boolean,
) {
    /** This build of TaffyGo has no copy of this part to download. */
    NOT_AVAILABLE_FOR_THIS_DEVICE("not-available-for-this-device", canRetry = false),

    /** The part exists in the product and has not been published yet. */
    NOT_PUBLISHED("not-published", canRetry = false),

    /** The download was tried as many times as it may be. */
    ATTEMPTS_SPENT("attempts-spent", canRetry = true),

    /** What arrived was not what this build of TaffyGo expects. */
    WRONG_CONTENTS("wrong-contents", canRetry = true),

    /** The device is offline. */
    CONNECTION_NOT_ALLOWED("connection-not-allowed", canRetry = true),

    /** You turned this part off. */
    TURNED_OFF("turned-off", canRetry = false),

    /**
     * The product's own catalog is wrong about this part.
     *
     * Shown rather than hidden. It cannot be fixed from the phone, and a part
     * that silently never appears is worse than one that says it is broken.
     */
    PRODUCT_DEFECT("product-defect", canRetry = false),
}
