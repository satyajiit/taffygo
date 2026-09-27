// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets.internal

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.model.TaffyConnectionCost
import com.taffygo.browser.ui.core.model.TaffyPart
import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.model.TaffyPartsState
import taffy.core_api.AssetDeliveryView
import taffy.core_api.AssetKindView
import taffy.core_api.AssetNetworkCostView
import taffy.core_api.AssetPresenceView
import taffy.core_api.AssetRefusalView
import taffy.core_api.AssetViewState

/**
 * Contract vocabulary to the words this app uses.
 *
 * The one place a delivery verdict is renamed. Three of the contract's refusal
 * reasons say the same thing to a person — the product's own catalog is wrong
 * about this part — so they land on one value; every other one is carried
 * across as itself. Collapsing further would lose the difference between "your
 * connection" and "our mistake", which is the only distinction a person can
 * act on.
 */
internal fun AssetDeliveryView.toUiParts(): TaffyPartsState = TaffyPartsState(
    // This projection exists only because a delivery view arrived, so it is
    // the one place that can say so. Absence of a view keeps the default.
    answered = true,
    supported = platform_supported,
    connectionCost = network_cost.toUiCost(),
    parts = assets.map(AssetViewState::toUiPart),
)

internal fun AssetProgressReport.toUiProgress(): TaffyPartProgress = TaffyPartProgress(
    id = TaffyPartId(assetId),
    version = assetRevision,
    downloadedBytes = writtenBytes.toLong(),
    totalBytes = totalBytes.toLong(),
)

private fun AssetViewState.toUiPart(): TaffyPart = TaffyPart(
    id = TaffyPartId(asset_id),
    version = asset_revision,
    purpose = kind.toUiPurpose(),
    availability = presence.toUiAvailability(),
    downloadedBytes = written_bytes.toLong(),
    totalBytes = total_bytes.toLong(),
    attempts = attempts.toInt(),
    hold = refusal?.reason?.toUiHold(),
)

private fun AssetKindView.toUiPurpose(): TaffyPartPurpose = when (this) {
    AssetKindView.PYTHON_STDLIB -> TaffyPartPurpose.PYTHON_LIBRARY
    AssetKindView.PYTHON_PACKAGES -> TaffyPartPurpose.PYTHON_PACKAGES
    AssetKindView.MODEL_WEIGHTS -> TaffyPartPurpose.MODEL
    AssetKindView.MODEL_TOKENIZER -> TaffyPartPurpose.MODEL_TOKENIZER
    AssetKindView.FILTER_LIST -> TaffyPartPurpose.BLOCK_LIST
    AssetKindView.COUNTRY_FLAGS -> TaffyPartPurpose.COUNTRY_FLAGS
    AssetKindView.START_SCENES -> TaffyPartPurpose.START_SCENES
}

private fun AssetPresenceView.toUiAvailability(): TaffyPartAvailability = when (this) {
    AssetPresenceView.ABSENT -> TaffyPartAvailability.MISSING
    AssetPresenceView.PARTIAL -> TaffyPartAvailability.PARTIAL
    AssetPresenceView.COMPLETE -> TaffyPartAvailability.CHECKING
    AssetPresenceView.INSTALLED -> TaffyPartAvailability.READY
}

private fun AssetNetworkCostView.toUiCost(): TaffyConnectionCost = when (this) {
    AssetNetworkCostView.OFFLINE -> TaffyConnectionCost.OFFLINE
    AssetNetworkCostView.METERED -> TaffyConnectionCost.MOBILE_DATA
    AssetNetworkCostView.UNMETERED -> TaffyConnectionCost.WIFI
}

private fun AssetRefusalView.toUiHold(): TaffyPartHold = when (this) {
    AssetRefusalView.NO_VARIANT_FOR_PLATFORM ->
        TaffyPartHold.NOT_AVAILABLE_FOR_THIS_DEVICE
    AssetRefusalView.NOT_PUBLISHED_YET -> TaffyPartHold.NOT_PUBLISHED
    AssetRefusalView.ATTEMPTS_EXHAUSTED -> TaffyPartHold.ATTEMPTS_SPENT
    AssetRefusalView.INTEGRITY_FAILED -> TaffyPartHold.WRONG_CONTENTS
    AssetRefusalView.NETWORK_NOT_PERMITTED -> TaffyPartHold.CONNECTION_NOT_ALLOWED
    AssetRefusalView.DECLINED_BY_PERSON -> TaffyPartHold.TURNED_OFF
    AssetRefusalView.UNKNOWN_ASSET,
    AssetRefusalView.CATALOG_ROW_INCOMPLETE,
    AssetRefusalView.VARIANT_TOO_LARGE,
    -> TaffyPartHold.PRODUCT_DEFECT
}
