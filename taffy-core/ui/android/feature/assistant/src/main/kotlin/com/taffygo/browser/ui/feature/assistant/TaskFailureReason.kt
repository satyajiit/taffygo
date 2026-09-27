// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import taffy.core_api.CoreFailureCode

/**
 * Why a task failed, as far as a surface tells reasons apart.
 *
 * Closed, so the generated failure code stays out of UI state (the
 * `TaskStartFailure` pattern). Each member has its own sentence, because the
 * person's next move differs: a refused key and an unreachable provider are
 * things they can fix, and those two offer set-up; the rest say what ran out
 * or what was refused. There is no catch-all member — a code with no sentence
 * of its own is grouped under [INTERNAL] here, by name, so the grouping is a
 * decision a reader can see rather than a default a new code falls into.
 */
enum class TaskFailureReason {
    /** The provider did not answer, or nothing is set up to answer. */
    PROVIDER_UNAVAILABLE,

    /** The provider refused the key or the request. */
    PROVIDER_REFUSED,

    /** The provider's quota or capacity was reached. */
    PROVIDER_LIMIT,

    /** The request never reached the provider. */
    OFFLINE,

    /** The task spent its budget of steps. */
    BUDGET_EXCEEDED,

    /** The task ran past its deadline. Time, not steps: a different budget. */
    DEADLINE_EXCEEDED,

    /** The task's moves were refused until it could not go on. */
    POLICY_REFUSED,

    /** A step's outcome on the page could not be confirmed. */
    UNVERIFIABLE_ACTION,

    /**
     * A step may or may not have happened, and Taffy will not repeat it.
     *
     * Not [INTERNAL], which it used to be grouped under. Nothing inside Taffy
     * broke: the core refuses to guess whether a move took effect, and says
     * so with this code while the task is still held as well as after it
     * ends. A phone showed "Taffy stopped unexpectedly" as both the heading
     * and the sentence of one card for exactly this (task `1bbba5cb`, a link
     * the browser refused on purpose), which is two false claims in a row.
     */
    OUTCOME_UNKNOWN,

    /** The pages could not be read, or not enough of them. */
    SOURCES_UNAVAILABLE,

    /**
     * Something inside Taffy failed: its own record of the task could not be
     * trusted, or a code the core does not send for a task arrived anyway.
     */
    INTERNAL,
    ;

    /** Whether the person can fix this from AI & providers. */
    val offersSetup: Boolean
        get() = this == PROVIDER_UNAVAILABLE || this == PROVIDER_REFUSED
}

internal fun CoreFailureCode?.toTaskFailureReason(): TaskFailureReason? = when (this) {
    null -> null
    CoreFailureCode.PROVIDER_UNAVAILABLE -> TaskFailureReason.PROVIDER_UNAVAILABLE
    CoreFailureCode.PROVIDER_REFUSED -> TaskFailureReason.PROVIDER_REFUSED
    CoreFailureCode.PROVIDER_LIMIT -> TaskFailureReason.PROVIDER_LIMIT
    CoreFailureCode.OFFLINE -> TaskFailureReason.OFFLINE
    CoreFailureCode.BUDGET_EXCEEDED -> TaskFailureReason.BUDGET_EXCEEDED
    CoreFailureCode.DEADLINE_EXCEEDED -> TaskFailureReason.DEADLINE_EXCEEDED
    CoreFailureCode.POLICY_REFUSED,
    CoreFailureCode.POLICY_DENIED,
    -> TaskFailureReason.POLICY_REFUSED
    CoreFailureCode.UNVERIFIABLE_ACTION -> TaskFailureReason.UNVERIFIABLE_ACTION
    CoreFailureCode.OUTCOME_UNKNOWN -> TaskFailureReason.OUTCOME_UNKNOWN
    CoreFailureCode.SOURCES_UNAVAILABLE -> TaskFailureReason.SOURCES_UNAVAILABLE
    // A journal that cannot be trusted is the one genuine internal failure the
    // core reports on a task (`project_failure` in the core runtime). The rest
    // are codes of the service's own requests that no task projection carries;
    // they are grouped here so that one arriving anyway still reads as Taffy's
    // fault and not the page's.
    CoreFailureCode.JOURNAL_UNUSABLE,
    CoreFailureCode.INTERNAL,
    CoreFailureCode.CANCELLED,
    CoreFailureCode.CORE_UNAVAILABLE,
    CoreFailureCode.INVALID_REQUEST,
    CoreFailureCode.BACKPRESSURE,
    -> TaskFailureReason.INTERNAL
}
