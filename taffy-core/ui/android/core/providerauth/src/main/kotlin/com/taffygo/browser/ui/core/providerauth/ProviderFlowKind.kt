// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/** The shape of one vendor's sign-in, fixed by the binary. */
enum class ProviderFlowKind {
    /**
     * RFC 8628 device authorization: the browser shows a short code, the
     * person enters it on the vendor's site, and the browser polls for the
     * grant. No redirect ever returns to the product.
     */
    DEVICE_CODE,

    /**
     * Authorization-code with PKCE through a Custom Tab: the vendor's own
     * page authorizes, and the redirect returns through the product's
     * registered callback.
     */
    PKCE,
}
