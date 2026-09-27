// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

/** Exact-revision mutations of Chromium-owned saved details and sign-ins. */
interface SavedDataCoreApiClient {
    /** Add or replace one Chromium-owned address profile at the shown revision. */
    suspend fun upsertSavedDetail(
        expectedRevision: ULong,
        detailId: String?,
        givenName: String,
        familyName: String,
        email: String,
        phone: String,
        address: String,
        postcode: String,
        country: String,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Delete one Chromium-owned address profile at the shown revision. */
    suspend fun deleteSavedDetail(detailId: String, expectedRevision: ULong) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Delete saved sign-in metadata without ever requesting its secret. */
    suspend fun deleteSavedSignIn(signInId: String, expectedRevision: ULong) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}
