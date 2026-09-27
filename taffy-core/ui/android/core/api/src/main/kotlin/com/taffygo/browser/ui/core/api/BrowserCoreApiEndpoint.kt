// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import java.io.Closeable
import kotlinx.coroutines.flow.StateFlow

/**
 * Browser-owned endpoint that submits UI intents and publishes validated state.
 *
 * Chromium supplies this interface with the Profile component only after all
 * generated commands and immutable state projections have production owners.
 * There is no default or preview implementation in the product graph.
 */
interface BrowserCoreApiEndpoint : CoreApiClient, Closeable {
    /** Transport health is distinct from generated product state. */
    val endpointFailure: StateFlow<CoreApiEndpointFailure?>

    /** Browser transport submodule for Chromium-owned personal data. */
    val savedData: SavedDataCoreApiClient

    override suspend fun upsertSavedDetail(
        expectedRevision: ULong,
        detailId: String?,
        givenName: String,
        familyName: String,
        email: String,
        phone: String,
        address: String,
        postcode: String,
        country: String,
    ) = savedData.upsertSavedDetail(
        expectedRevision,
        detailId,
        givenName,
        familyName,
        email,
        phone,
        address,
        postcode,
        country,
    )

    override suspend fun deleteSavedDetail(detailId: String, expectedRevision: ULong) =
        savedData.deleteSavedDetail(detailId, expectedRevision)

    override suspend fun deleteSavedSignIn(signInId: String, expectedRevision: ULong) =
        savedData.deleteSavedSignIn(signInId, expectedRevision)
}
