// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The vendor-neutral role a model may serve.
 *
 * The model router under `taffy-core/components/intelligence/core/rust/` is
 * the authority for this vocabulary. These values match the generated
 * portable contract, so a route cannot mean one thing in a screen and another
 * in Rust.
 *
 * A role names what a model is for, never how it works. The provider and the
 * route are shown beside it, and the plain wording a person reads lives in the
 * string catalogue rather than here.
 */
enum class ModelRole(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Planning, analysis, and result synthesis. Hard questions. */
    PRIMARY_REASONING("primary_reasoning"),

    /** Page triage and extraction help, where latency is the point. Fast steps. */
    FAST_BROWSING("fast_browsing"),

    /** The separately permitted image path. Images and screenshots. */
    VISION("vision"),

    /** Retrieval support: finding things in what has already been read. */
    EMBEDDING("embedding"),
}
