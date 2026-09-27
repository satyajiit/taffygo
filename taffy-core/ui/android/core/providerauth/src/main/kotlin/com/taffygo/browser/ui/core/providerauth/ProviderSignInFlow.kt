// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** One vendor's compiled sign-in flow facts. */
data class ProviderSignInFlow(
    /** The catalog identity the flow signs in to. */
    val providerId: String,
    /** The flow shape this binary runs for it. */
    val kind: ProviderFlowKind,
    /**
     * Whether the browser process will actually start this flow.
     *
     * A flow being carried here means the shape is built; it does not mean it
     * runs. `ProviderAuthVendorRegistered` in
     * `taffy-core/browser/providerauth/provider_auth_configuration.cc` refuses
     * a vendor whose terms review is undated, and one that presents a client
     * identity it does not carry — so a complete, tested flow can be present
     * in the binary and still be refused at the moment it is asked to begin.
     *
     * It is restated here rather than asked at press time because a surface
     * has to decide what to *draw*, and it draws before anybody presses. With
     * only the flow map to read, four vendors offered a Sign in button that
     * answered "not available on this device" — which is not what happened and
     * not something the person could act on.
     *
     * Two facts folded into one answer, and deliberately so: the date and the
     * identity are independent, but neither alone is what a surface needs to
     * know. `check_vendor_agreement.py` computes the browser's own answer from
     * `kVendors` and fails when this column disagrees, so the duplicate cannot
     * drift — which is the same bargain decision 0081 already strikes for the
     * flow shape sitting in two languages.
     */
    val browserWillStart: Boolean,
)
