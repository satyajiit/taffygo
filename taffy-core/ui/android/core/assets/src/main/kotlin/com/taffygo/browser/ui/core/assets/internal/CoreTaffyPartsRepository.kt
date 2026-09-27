// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.api.submitCoreApiCommand
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartsState
import java.util.concurrent.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.AssetDeliveryView
import taffy.core_api.CoreStatus
import taffy.core_api.PartMemberStatus

/** Profile-owned projection over immutable Core API delivery status. */
internal class CoreTaffyPartsRepository(
    private val core: CoreApiClient,
    lifetime: TaffyProfileLifetime,
) : TaffyPartsRepository {

    override val state: StateFlow<TaffyPartsState> = core.status
        .distinctUntilChangedBy(CoreStatus::partsProjectionVersion)
        .map { status -> status.completeAssetDelivery()?.toUiParts() ?: TaffyPartsState() }
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = core.status.value.completeAssetDelivery()?.toUiParts()
                ?: TaffyPartsState(),
        )

    override val progress: Flow<TaffyPartProgress> =
        core.assetProgress.map { it.toUiProgress() }

    override suspend fun readMember(id: TaffyPartId, memberPath: String): ByteArray? = try {
        // No version, unlike request and remove. Those name a revision because
        // the core acts on an exact one; this asks for bytes that are already
        // on the device, and naming a revision would let a screen ask for one
        // that is not installed and get nothing it could act on.
        core.readPartMember(id.value, memberPath)
            .takeIf { it.status == PartMemberStatus.OK }
            ?.bytes
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (_: CoreApiSubmissionException) {
        null
    } catch (_: RuntimeException) {
        null
    }

    override suspend fun request(id: TaffyPartId): TaffyResult<Unit> =
        withKnownPart(id) { version ->
            submitCoreApiCommand { core.requestAsset(id.value, version) }
        }

    override suspend fun remove(id: TaffyPartId): TaffyResult<Unit> =
        withKnownPart(id) { version ->
            submitCoreApiCommand { core.removeAsset(id.value, version) }
        }

    // No `withKnownPart`, and that is the whole point of this one: it is what
    // a surface has left when the snapshot names no parts at all.
    override suspend fun retryCore(): TaffyResult<Unit> =
        submitCoreApiCommand { core.retryCore() }

    /**
     * Runs [command] with the version the snapshot names for [id].
     *
     * A part is asked for by identity, and the version has to be the one the
     * core currently holds — sending a version a screen remembered from an
     * earlier snapshot would name a part that is no longer the one on offer.
     */
    private suspend fun withKnownPart(
        id: TaffyPartId,
        command: suspend (version: String) -> TaffyResult<Unit>,
    ): TaffyResult<Unit> {
        if (!core.status.value.hasCompleteProjection()) {
            return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        }
        val version = state.value.parts.firstOrNull { it.id == id }?.version
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        return command(version)
    }
}

/** A delivery view has no aggregate revision, so its complete immutable record is the key. */
internal data class PartsProjectionVersion(
    val complete: Boolean,
    val delivery: AssetDeliveryView?,
)

internal fun CoreStatus.partsProjectionVersion() = PartsProjectionVersion(
    complete = hasCompleteProjection(),
    delivery = completeAssetDelivery(),
)

private fun CoreStatus.completeAssetDelivery(): AssetDeliveryView? =
    asset_delivery.takeIf { hasCompleteProjection() }
