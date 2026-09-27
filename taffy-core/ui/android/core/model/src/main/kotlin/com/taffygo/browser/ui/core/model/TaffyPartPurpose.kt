// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * What one downloaded part of Taffy is for (screen SCR-203).
 *
 * A closed set that mirrors the delivery catalog. Adding a part to the product
 * adds a row to the catalog and a case here; nothing else on the screen needs
 * to change, because every part is rendered from these facts alone.
 */
enum class TaffyPartPurpose(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /**
     * Whether the product fetches this part on its own.
     *
     * A required part arrives from the moment the profile exists, on any
     * live connection, with nothing to tap and nothing to delete: the
     * profile's installer asks for it, and asks again when the browser has
     * tried and stopped. Everything else is asked for by the surface that
     * needs it, and screen SCR-203 lets a person delete it.
     */
    val required: Boolean,
) {
    /**
     * The Python library Taffy runs its own tools from. Required, because
     * page intelligence cannot start without it and screens SCR-101 and
     * SCR-102 hold the start page until it is installed.
     */
    PYTHON_LIBRARY("python-library", required = true),

    /** The extra Python packages those tools may import. */
    PYTHON_PACKAGES("python-packages", required = false),

    /** An on-device model. */
    MODEL("model", required = false),

    /** An on-device model's tokenizer. */
    MODEL_TOKENIZER("model-tokenizer", required = false),

    /**
     * A list of ads and trackers to block. Required, because blocking is on
     * by default (decision 0076) and a profile that is blocking with no
     * rules is blocking nothing; the copy bundled in the APK stands in only
     * until the published pack has arrived.
     */
    BLOCK_LIST("block-list", required = true),

    /**
     * The country flag pictures a country picker draws. Required, because
     * the first-run language chip and screen SCR-006 draw flags from it,
     * and a picker that fetches two hundred pictures at the moment it opens
     * is a picker that opens empty.
     */
    COUNTRY_FLAGS("country-flags", required = true),

    /**
     * The painted plates the start page draws above its wordmark, one for each
     * part of the day. Required for the same reason the flags are: screens
     * SCR-101 and SCR-102 draw one on every new tab, and a page that fetched
     * its picture when it opened would open empty. The plate compiled into the
     * installer stands in until this pack has arrived.
     */
    START_SCENES("start-scenes", required = true),
}
