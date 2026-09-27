// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartsState
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

internal class FakeDownloadRepository(
    initial: DownloadSnapshot = DownloadSnapshot.LOADING,
) : DownloadRepository {
    override val snapshot: MutableStateFlow<DownloadSnapshot> = MutableStateFlow(initial)
    val actions = mutableListOf<Pair<DownloadId, DownloadAction>>()
    var acceptsActions: Boolean = true

    override suspend fun perform(id: DownloadId, action: DownloadAction): Boolean {
        actions += id to action
        return acceptsActions
    }
}

internal class FakeDownloadParts : TaffyPartsRepository {
    override val state: StateFlow<TaffyPartsState> = MutableStateFlow(TaffyPartsState())
    override val progress: Flow<TaffyPartProgress> = MutableSharedFlow()
    val requested = mutableListOf<TaffyPartId>()
    val removed = mutableListOf<TaffyPartId>()

    override suspend fun readMember(id: TaffyPartId, memberPath: String): ByteArray? = null

    override suspend fun request(id: TaffyPartId): TaffyResult<Unit> {
        requested += id
        return TaffyResult.Success(Unit)
    }

    override suspend fun retryCore(): TaffyResult<Unit> = TaffyResult.Success(Unit)

    override suspend fun remove(id: TaffyPartId): TaffyResult<Unit> {
        removed += id
        return TaffyResult.Success(Unit)
    }
}

internal class DownloadTestDispatchers(
    dispatcher: CoroutineDispatcher,
) : AppDispatchers {
    override val main: CoroutineDispatcher = dispatcher
    override val default: CoroutineDispatcher = dispatcher
    override val io: CoroutineDispatcher = dispatcher
}

internal class DownloadTestAnalytics : AnalyticsClient {
    val events = mutableListOf<AnalyticsEvent>()
    override fun record(event: AnalyticsEvent) {
        events += event
    }
    override fun recent(): List<AnalyticsEvent> = events.toList()
}
