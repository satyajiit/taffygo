// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * Whether one provider's page can offer the vendor's sign-in.
 *
 * Two facts decide it and they are independent: the catalog says the provider
 * accepts a plan sign-in (decision 0080), and this binary says it compiles that
 * vendor's flow (decision 0081). Both are needed, so there are three answers
 * rather than a boolean.
 *
 * [NOT_BUILT] is the one that must never be collapsed. A vendor the catalog
 * offers OAUTH for and this build has no flow for is explained where the
 * sign-in would have been — not degraded into a key form, which would hand
 * somebody a field for a credential the vendor does not issue.
 */
enum class ProviderSignInOffer {
    /** The provider names no plan sign-in at all. */
    NONE,

    /** The vendor's flow is compiled here and can be started. */
    OFFERED,

    /** The catalog offers it and this version of TaffyGo cannot run it. */
    NOT_BUILT,
}
