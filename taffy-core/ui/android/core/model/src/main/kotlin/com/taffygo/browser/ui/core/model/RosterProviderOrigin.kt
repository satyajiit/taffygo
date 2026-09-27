// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** Whether a roster row came from the catalog or is the person's own. */
enum class RosterProviderOrigin {
    /** Shipped or served by the catalog. */
    CATALOG,

    /** Defined by the person. */
    CUSTOM,
}
