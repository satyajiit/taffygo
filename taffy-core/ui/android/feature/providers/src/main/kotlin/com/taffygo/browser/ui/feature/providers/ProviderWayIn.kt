// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * How a person could reach a model through one provider.
 *
 * The roster states this as a list of methods plus an origin. A row carries
 * the answer instead of the list, so a screen never re-derives it and two
 * surfaces can never read the same methods differently.
 *
 * [NONE] is a catalog defect rather than a state a working provider is in, and
 * it is carried so the row can say so on its face.
 */
enum class ProviderWayIn {
    /** A key the person pastes. */
    KEY,

    /** A plan the person signs in to with the vendor. */
    PLAN,

    /** Either, and one credential at a time (decision 0029). */
    KEY_OR_PLAN,

    /** An address the person supplied. */
    OWN_ENDPOINT,

    /** The provider's catalog entry names no method at all. */
    NONE,
}
