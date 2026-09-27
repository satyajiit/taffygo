// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The few behavioural facts about one provider a setup surface cannot derive.
 *
 * Every field is absent whenever the catalog said nothing, and a surface told
 * nothing shows nothing rather than a placeholder. No prose is carried here:
 * what to try when a key is refused is written in the product's own strings, in
 * the person's own language, and is never served.
 */
data class ProviderPresentation(
    /**
     * What this vendor's keys begin with, so a form can say a pasted value does
     * not look like one before spending a call proving it.
     */
    val keyPrefix: String?,
    /** Where the person creates a key. https only, as the catalog serves it. */
    val getKeyUrl: String?,
    /** Where the vendor documents this endpoint. https only, as the catalog serves it. */
    val docsUrl: String?,
)
