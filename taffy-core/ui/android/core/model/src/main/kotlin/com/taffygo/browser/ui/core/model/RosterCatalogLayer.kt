// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** Which catalog layer supplied a roster row (decision 0080). */
enum class RosterCatalogLayer {
    /** Compiled into this release. */
    EMBEDDED_BASELINE,

    /** Served by the published catalog and cached on the device. */
    REMOTE_OVERLAY,

    /** The person's own provider. */
    USER_OVERRIDE,
}
