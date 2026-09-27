// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One rung of the thinking ladder, ordered from least to most.
 *
 * The rungs are held equal to the model router's own ladder under
 * `taffy-core/components/intelligence/core/rust/`, member for member, so a rung
 * cannot mean one amount in a screen and another where the call is made.
 *
 * There is no member for Taffy deciding. That is the absence of a preference,
 * which is why every field carrying one is nullable rather than carrying a rung
 * that means no rung.
 */
enum class ThinkingLevel {
    /** No thinking phase. */
    OFF,

    /** The smallest thinking phase the provider offers. */
    MINIMAL,

    /** A short thinking phase. */
    LOW,

    /** The middle of the ladder. */
    MEDIUM,

    /** A long thinking phase. */
    HIGH,

    /** Above [HIGH], and offered only by a model whose catalog entry maps it. */
    XHIGH,

    /** The top of the ladder, and offered only by a model whose catalog entry maps it. */
    MAX,
}
