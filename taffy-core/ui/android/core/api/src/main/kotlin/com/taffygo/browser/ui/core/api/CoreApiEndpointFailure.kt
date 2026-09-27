// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.CoreStatusPayloadCodecError

/** Closed reasons why a browser CoreStatus snapshot could not be trusted. */
sealed interface CoreApiEndpointFailure {
    data object Disconnected : CoreApiEndpointFailure
    data object EnvelopeMismatch : CoreApiEndpointFailure
    data object MissingReadyPayload : CoreApiEndpointFailure
    data object StaleSnapshot : CoreApiEndpointFailure
    data class InvalidPayload(
        val reason: CoreStatusPayloadCodecError?,
    ) : CoreApiEndpointFailure
}
