// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.credentials

/**
 * Proves one pasted key with one bounded model call (decision
 * `docs/decisions/0083-a-pasted-key-is-proved-by-one-bounded-completion.md`).
 *
 * The draft's bytes go in and a [ProviderProbeVerdict] comes back; nothing
 * durable changes either way. The implementation seals the bytes as a
 * one-shot transient in the browser's vault, hands the core the opaque
 * handle, and waits for the verdict the core publishes — the material never
 * enters durable records and the send spends the handle exactly once, so a
 * draft that was probed and not saved leaves nothing behind.
 */
interface ProviderKeyProber {

    /**
     * Probes [material] against [providerId]'s catalog endpoint.
     *
     * Returns the verdict, [ProviderProbeVerdict.UNKNOWN] when no verdict
     * landed in time. The caller keeps ownership of [material] — it is not
     * zeroed here, because a definitive [ProviderProbeVerdict.USABLE] is
     * usually followed by saving the same bytes. Throws
     * `CoreApiSubmissionException` when the core refuses to run the probe at
     * all (one probe flies at a time).
     */
    suspend fun probeApiKey(providerId: String, material: ByteArray): ProviderProbeVerdict
}
