// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * What screen SCR-404 has to say when it has no useful list to draw.
 *
 * Three of these are the states a screen must triage separately
 * (`docs/architecture/android-app-architecture.md` section 4): still coming,
 * nothing there, and something is wrong. They are told apart by facts rather
 * than by an empty list, because an empty list is what all three look like.
 *
 * [BLOCKED] is this screen's failure arm. The roster carries no error channel —
 * a core that cannot answer has not published, which is [LOADING] — so the only
 * failure SCR-404 can honestly report is the one the roster can express: rows
 * arrived and not one of them can be acted on.
 */
enum class ProviderHubStatus {
    /** The core has not published a roster yet. */
    LOADING,

    /** The core published, and the merged catalog carries no provider. */
    EMPTY,

    /**
     * The catalog carries providers and every one of them is already
     * connected, so this screen has nothing left to add.
     *
     * Told apart from [EMPTY] because the two are opposite facts that draw the
     * same blank page: one says the provider list is empty, which would be a
     * lie about a phone with eighteen providers set up on it.
     */
    ALL_CONNECTED,

    /** Providers arrived, and every one of them is blocked. */
    BLOCKED,

    /** At least one provider can be set up. */
    READY,
}
