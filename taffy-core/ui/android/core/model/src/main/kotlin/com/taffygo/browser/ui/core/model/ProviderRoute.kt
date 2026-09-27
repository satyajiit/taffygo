// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Where a model request goes (screen SCR-404). The screen states this in the
 * user's words; the type is what the screen states it from.
 */
enum class ProviderRoute(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Nothing is configured, so nothing leaves the device. */
    NOT_CONFIGURED("not_configured"),

    /** Requests go directly to the provider with the user's own key. */
    DIRECT_WITH_YOUR_KEY("direct_with_your_key"),

    /** The reviewed local workflow does not invoke a model. */
    NO_MODEL_REQUIRED("no_model_required"),
}
