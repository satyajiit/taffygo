// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Why a page is not showing (screen SCR-108). Upstream Chromium owns the
 * behaviour; TaffyGo owns the wording, and the wording is honest about which
 * of these happened rather than showing one page for all of them.
 */
enum class PageLoadFailure(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /** Whether retrying could plausibly succeed without the user changing anything. */
    val retryable: Boolean,
) {
    /** The device has no network. */
    OFFLINE("offline", retryable = true),

    /** The host name did not resolve. */
    NAME_NOT_RESOLVED("name_not_resolved", retryable = true),

    /**
     * The name resolved, and nothing at that address answered — refused,
     * reset, unreachable, or a generic failed load whose only honest
     * description is that the site could not be reached.
     */
    UNREACHABLE("unreachable", retryable = true),

    /** The server did not answer in time. */
    TIMED_OUT("timed_out", retryable = true),

    /**
     * The connection is not private. Retrying does not make a certificate
     * valid, so the error page offers understanding rather than a retry.
     */
    CERTIFICATE_INVALID("certificate_invalid", retryable = false),

    /**
     * The page loaded and then its process died — the engine's sad tab.
     *
     * Not a load failure like the five above, and named anyway: the person is
     * looking at a page-shaped hole either way, and this is the enum that
     * decides which words stand in it. The page itself was fine, so a reload
     * usually works — the renderer is simply started again.
     */
    PAGE_CRASHED("page_crashed", retryable = true),
}
