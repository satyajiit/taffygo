// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartsState
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

/**
 * The parts TaffyGo downloads for itself, and what may be done about them.
 *
 * Kept apart from the browser's own downloads on purpose. These are files the
 * product fetches so it can work — a Python library, a model, a block list —
 * and the person did not ask for any of them, cannot open them, and is only
 * shown them so that nothing about their device or their data allowance is
 * happening out of sight.
 *
 * Nothing here downloads anything. Every method states an intent and the
 * isolated core decides; what comes back on [state] is what the browser
 * observed, never what this layer hoped for.
 */
interface TaffyPartsRepository {

    /** Every part on this device, and what the connection costs. */
    val state: StateFlow<TaffyPartsState>

    /**
     * How far a running download has got, as the bytes land.
     *
     * A separate flow because it arrives by a separate route: the browser
     * pushes it directly, several times a second, and the isolated core is
     * not asked. [state] is the durable answer and this is the moving one.
     */
    val progress: Flow<TaffyPartProgress>

    /**
     * Reads one file out of one installed part, by name inside the part.
     *
     * Null for every reason a caller cannot act on — the part is not
     * downloaded, it carries no such name, it could not be read, the browser
     * could not be reached. A surface showing a picture has the same thing to
     * draw in all four cases, and the difference is on the delivery screen,
     * where a person can do something about it.
     *
     * Nothing is cached here. A screen that needs the same file repeatedly
     * holds its own; caching in a profile-lifetime object would keep whatever
     * was ever asked for until the profile closed.
     */
    suspend fun readMember(id: TaffyPartId, memberPath: String): ByteArray?

    /**
     * Ask for one part now.
     *
     * The profile's installer says this for every required part, and again
     * whenever the browser has tried one and stopped. A surface says it only
     * for a part it needs that start-up would not have fetched.
     */
    suspend fun request(id: TaffyPartId): TaffyResult<Unit>

    /**
     * Ask the browser to start its core again and describe delivery afresh.
     *
     * The only thing a surface can do when [state] has never said anything.
     * Every part on this screen is a fact the core produced, so a core that
     * is not running has no parts to name and no part to be asked for — and
     * a screen waiting on one of them is waiting on a list that will stay
     * empty until this is called.
     */
    suspend fun retryCore(): TaffyResult<Unit>

    /** Delete one part's files from this device. */
    suspend fun remove(id: TaffyPartId): TaffyResult<Unit>
}
