// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** One native-owned operation, bound to the visible regular-profile window. */
interface BackupRecoveryKeySession {
    enum class Mode { CREATE, RESTORE }
    enum class Acceptance { ACCEPTED, REFUSED, UNAVAILABLE }
    enum class Outcome { CONFIRMED, CANCELLED, UNAVAILABLE }

    val mode: Mode

    /** One-shot mutable text for the trusted Android view, never saved or logged. */
    fun takeGeneratedKeyForDisplay(): CharArray?

    /** Only after the generated key was shown and the person confirms keeping it. */
    fun confirmKeyRetained(): Acceptance

    /** Native parses and retains the key; no archive crypto or blocking IO runs here. */
    fun acceptEnteredKey(text: CharArray): Acceptance

    /**
     * Idempotent, nonthrowing withdrawal of this caller's interest. Native clears
     * reversible key/stage custody before consumptive dispatch. Once authority
     * has been requested, withdrawal never aborts or repeats physical work;
     * native retains custody until it drains or requires recovery.
     */
    fun cancel()
}
