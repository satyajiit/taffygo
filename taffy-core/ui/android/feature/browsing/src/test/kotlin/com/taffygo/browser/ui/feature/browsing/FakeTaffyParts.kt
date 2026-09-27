// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.model.TaffyPartsState
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow

/**
 * A delivery plane a host test supplies. Nothing is installed unless the
 * caller says so, which is the closed answer the start page waits on.
 */
class FakeTaffyParts(
    initial: TaffyPartsState = TaffyPartsState(),
) : TaffyPartsRepository {
    val requested = mutableListOf<TaffyPartId>()
    var coreRetries: Int = 0
        private set

    override val state: StateFlow<TaffyPartsState> = MutableStateFlow(initial)
    override val progress: Flow<TaffyPartProgress> = emptyFlow()

    override suspend fun readMember(id: TaffyPartId, memberPath: String): ByteArray? = null

    override suspend fun request(id: TaffyPartId): TaffyResult<Unit> {
        requested += id
        return TaffyResult.Success(Unit)
    }

    override suspend fun remove(id: TaffyPartId): TaffyResult<Unit> = TaffyResult.Success(Unit)

    override suspend fun retryCore(): TaffyResult<Unit> {
        coreRetries++
        return TaffyResult.Success(Unit)
    }
}

/** A plane that has already installed the Python library. */
fun readyPythonParts(): FakeTaffyParts = FakeTaffyParts(
    TaffyPartsState(
        answered = true,
        supported = true,
        parts = listOf(
            TaffyPart(
                id = TaffyPartId("python-stdlib"),
                version = "3.14.7-taffy.1",
                purpose = TaffyPartPurpose.PYTHON_LIBRARY,
                availability = TaffyPartAvailability.READY,
                downloadedBytes = 1,
                totalBytes = 1,
                attempts = 0,
                hold = null,
            ),
        ),
    ),
)
