// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/** The complete synchronous answer to a browser-role offer attempt. */
enum class BrowserRoleOfferResult {
    /** Android's role sheet was launched. The result remains Android's to decide. */
    SHEET_LAUNCHED,

    /** TaffyGo already owns the role, so no sheet was needed. */
    ALREADY_DEFAULT,

    /** This installation already showed the sheet once and will not nag. */
    ALREADY_OFFERED,

    /** Android does not expose the browser role on this device. */
    UNSUPPORTED,

    /** The accepted output no longer has a visible Activity from which to ask. */
    NOT_VISIBLE,

    /** Android did not accept the request. No successful offer was recorded. */
    FAILED,
}
