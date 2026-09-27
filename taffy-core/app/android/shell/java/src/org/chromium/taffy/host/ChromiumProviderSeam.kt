// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.ProviderOauthRecord
import com.taffygo.browser.ui.core.providerauth.ProviderFlowEventKind
import org.chromium.taffy.browser.account.mojom.ProviderAccessResult
import org.chromium.taffy.browser.account.mojom.ProviderFlowEventKind as MojoProviderFlowEventKind
import org.chromium.taffy.browser.account.mojom.ProviderOauthRecord as MojoProviderOauthRecord

/*
 * The provider half of [ChromiumProfilePlatformAdapter]'s translation, as
 * plain functions.
 *
 * They live beside the adapter rather than inside it because they are the
 * only part of it that is a pure mapping between two vocabularies — the
 * account plane's Mojo shapes and the store's own — and because the adapter
 * is the profile's whole platform seam and every method it gains makes the
 * mapping harder to find. Nothing here touches the store, the network, or a
 * callback; each function answers one question about one value.
 */

/**
 * The wire record as the store's triple, without copying the token buffers.
 *
 * The two views share one set of arrays on purpose: encoding inside the
 * coordinator's critical section makes the store's copy, and the caller's
 * `zero()` afterwards wipes the buffers both views point at.
 */
internal fun MojoProviderOauthRecord.asStoreRecord(): ProviderOauthRecord =
    ProviderOauthRecord(
        tokenType = tokenType,
        expiresAtEpochMs = expiresAtEpochMs,
        scopes = scopes,
        accessToken = accessToken,
        refreshToken = refreshToken,
        credentialHost = credentialHost,
    )

/** One access answer, in the shape the browser process reads. */
internal fun providerAccessResult(
    kind: Int,
    material: ByteArray,
    expiresAtEpochMs: Long,
    credentialHost: String?,
): ProviderAccessResult = ProviderAccessResult().apply {
    this.kind = kind
    this.material = material
    this.expiresAtEpochMs = expiresAtEpochMs
    this.credentialHost = credentialHost
}

/** One sign-in lifecycle event, in the visible engine's vocabulary. */
internal fun engineFlowEventKind(kind: Int): ProviderFlowEventKind = when (kind) {
    MojoProviderFlowEventKind.AWAITING_AUTHORIZATION ->
        ProviderFlowEventKind.AWAITING_AUTHORIZATION
    MojoProviderFlowEventKind.USER_CODE_READY -> ProviderFlowEventKind.USER_CODE_READY
    MojoProviderFlowEventKind.EXCHANGING -> ProviderFlowEventKind.EXCHANGING
    MojoProviderFlowEventKind.COMPLETED -> ProviderFlowEventKind.COMPLETED
    MojoProviderFlowEventKind.FAILED_DENIED -> ProviderFlowEventKind.FAILED_DENIED
    MojoProviderFlowEventKind.FAILED_PROVIDER -> ProviderFlowEventKind.FAILED_PROVIDER
    MojoProviderFlowEventKind.FAILED_DEADLINE -> ProviderFlowEventKind.FAILED_DEADLINE
    MojoProviderFlowEventKind.FAILED_UNAVAILABLE -> ProviderFlowEventKind.FAILED_UNAVAILABLE
    // Mojo already validated the enum, so an unlisted value here is this
    // adapter falling behind the wire definition, not bad input.
    else -> error("Unrouted provider flow event kind $kind")
}
