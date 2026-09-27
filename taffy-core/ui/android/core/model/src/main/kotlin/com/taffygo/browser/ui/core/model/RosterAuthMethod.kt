// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** A way a provider on the roster can be paid for and reached. */
enum class RosterAuthMethod {
    /** A key the person pastes, sealed in the Android keystore. */
    API_KEY,

    /** A plan the person signs in to with the vendor. */
    OAUTH,
}
