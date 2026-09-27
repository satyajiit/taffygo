// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The part of screen SCR-415 the screen itself owns.
 *
 * Everything else on that page is the core's published answer. This is what is
 * true only here and only now: what has been typed, whether a call is out,
 * what the last one said, and whether the confirmation before a removal is
 * showing. None of it survives the screen, and none of it is ever the reason a
 * credential appears connected — that is the roster's word.
 *
 * [key] is a person's own text. It is never logged, never recorded in an
 * analytics payload, and never handed to anything but the store at the moment
 * of saving.
 */
data class ProviderConfigDraft(
    /** What has been typed into the key field. */
    val key: String = "",
    /** Whether the field is showing characters rather than dots. */
    val revealed: Boolean = false,
    /** Whether one bounded call is out. */
    val probing: Boolean = false,
    /** What the last attempt said, or null. */
    val problem: ProviderKeyProblem? = null,
    /**
     * Whether the last attempt ended with the store holding the key. Cleared
     * the moment anything is typed, because a form being edited is not a form
     * reporting a success.
     */
    val stored: Boolean = false,
    /** Whether the deliberate second step before a removal is showing. */
    val confirmingSignOut: Boolean = false,
    /** Whether the removal is running. */
    val signingOut: Boolean = false,
)
