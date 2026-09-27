// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** How a You-hub child screen's port answered. */
enum class YouSurfaceAvailability {
    /** The port has not answered yet. */
    LOADING,

    /** A complete answer, including an empty list. */
    READY,

    /** This phone cannot show the surface yet. */
    UNAVAILABLE,
}
