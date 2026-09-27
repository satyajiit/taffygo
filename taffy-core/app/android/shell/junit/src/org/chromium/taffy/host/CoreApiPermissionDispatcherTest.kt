// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import taffy.core_api.PlatformPermission

@RunWith(BaseRobolectricTestRunner::class)
class CoreApiPermissionDispatcherTest {
    @Test
    fun ambiguousOrAbsentResumedWindowRefusesPermission() =
        runBlocking(Dispatchers.Main.immediate) {
            val handled = mutableListOf<String>()
            val unavailable = mutableListOf<String>()
            val dispatcher = CoreApiPermissionDispatcher(
                invalidPermission = { error("wire value is valid") },
                deliverUnavailable = { requestId, _ -> unavailable += requestId },
            )
            val first = dispatcher.register { requestId, _ -> handled += "first:$requestId" }
            val second = dispatcher.register { requestId, _ -> handled += "second:$requestId" }

            dispatcher.accept("absent", PlatformPermission.CAMERA.wire.toInt())
            first.activate()
            dispatcher.accept("first", PlatformPermission.CAMERA.wire.toInt())
            second.activate()
            dispatcher.accept("ambiguous", PlatformPermission.CAMERA.wire.toInt())
            second.deactivate()
            dispatcher.accept("first-again", PlatformPermission.CAMERA.wire.toInt())

            assertEquals(listOf("first:first", "first:first-again"), handled)
            assertEquals(listOf("absent", "ambiguous"), unavailable)
        }

    @Test
    fun closingWindowSettlesEveryAssignedPermissionExactlyOnce() =
        runBlocking(Dispatchers.Main.immediate) {
            val unavailable = mutableListOf<String>()
            val dispatcher = CoreApiPermissionDispatcher(
                invalidPermission = { error("requests are well formed") },
                deliverUnavailable = { requestId, _ -> unavailable += requestId },
            )
            val window = dispatcher.register { _, _ -> }
            window.activate()

            dispatcher.accept("camera", PlatformPermission.CAMERA.wire.toInt())
            dispatcher.accept("location", PlatformPermission.LOCATION.wire.toInt())
            window.close()
            window.close()

            assertEquals(listOf("camera", "location"), unavailable)
            assertEquals(false, dispatcher.settle("camera", PlatformPermission.CAMERA))
        }

    @Test
    fun resultClaimRemovesPermissionFromWindowTeardown() =
        runBlocking(Dispatchers.Main.immediate) {
            val unavailable = mutableListOf<String>()
            val dispatcher = CoreApiPermissionDispatcher(
                invalidPermission = { error("requests are well formed") },
                deliverUnavailable = { requestId, _ -> unavailable += requestId },
            )
            val window = dispatcher.register { _, _ -> }
            window.activate()

            dispatcher.accept("camera", PlatformPermission.CAMERA.wire.toInt())
            assertEquals(true, dispatcher.settle("camera", PlatformPermission.CAMERA))
            window.close()

            assertEquals(emptyList<String>(), unavailable)
        }
}
