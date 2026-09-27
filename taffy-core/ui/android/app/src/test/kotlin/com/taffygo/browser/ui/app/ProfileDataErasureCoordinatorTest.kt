// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.feature.settings.ProfileDataControl
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

class ProfileDataErasureCoordinatorTest {
    @Test
    fun `authority is withdrawn and tasks are stopped before Android clear is requested`() = runTest {
        val calls = mutableListOf<String>()
        val coordinator = ProfileDataErasureCoordinator(
            withdrawWindowAndTabAuthority = { calls += "withdraw-windows" },
            cancelTask = { calls += "cancel-$it" },
            closeRemainingProfileAuthority = { calls += "close-profile" },
            destroyCredentialMaterial = { calls += "destroy-credentials" },
            requestApplicationDataClear = {
                calls += "request-android-clear"
                true
            },
        )

        val result = coordinator.erase(
            ProfileDataErasureCoordinator.ActiveTasks.Known(
                listOf("task-b", "task-a", "task-a"),
            ),
        )

        assertEquals(ProfileDataControl.DeletionResult.STARTED, result)
        assertEquals(
            listOf(
                "withdraw-windows",
                "cancel-task-a",
                "cancel-task-b",
                "close-profile",
                "destroy-credentials",
                "request-android-clear",
            ),
            calls,
        )
    }

    @Test
    fun `cleanup failure cannot skip stronger OS clear and rejection never claims deletion`() = runTest {
        val calls = mutableListOf<String>()
        val coordinator = ProfileDataErasureCoordinator(
            withdrawWindowAndTabAuthority = {
                calls += "withdraw-windows"
                error("close failed")
            },
            cancelTask = { calls += "cancel-$it" },
            closeRemainingProfileAuthority = { calls += "close-profile" },
            destroyCredentialMaterial = { calls += "destroy-credentials" },
            requestApplicationDataClear = {
                calls += "request-android-clear"
                false
            },
        )

        assertEquals(
            ProfileDataControl.DeletionResult.FAILED,
            coordinator.erase(ProfileDataErasureCoordinator.ActiveTasks.Known(listOf("task"))),
        )
        assertEquals("request-android-clear", calls.last())
        assertEquals(
            ProfileDataControl.DeletionResult.UNAVAILABLE,
            coordinator.erase(ProfileDataErasureCoordinator.ActiveTasks.Known(emptyList())),
        )
    }

    @Test
    fun `unknown task list still withdraws profile authority and requests Android clear`() = runTest {
        val calls = mutableListOf<String>()
        val coordinator = ProfileDataErasureCoordinator(
            withdrawWindowAndTabAuthority = { calls += "withdraw-windows" },
            cancelTask = { calls += "cancel-$it" },
            closeRemainingProfileAuthority = { calls += "close-profile" },
            destroyCredentialMaterial = { calls += "destroy-credentials" },
            requestApplicationDataClear = {
                calls += "request-android-clear"
                true
            },
        )

        assertEquals(
            ProfileDataControl.DeletionResult.STARTED,
            coordinator.erase(ProfileDataErasureCoordinator.ActiveTasks.Unknown),
        )
        assertEquals(
            listOf(
                "withdraw-windows",
                "close-profile",
                "destroy-credentials",
                "request-android-clear",
            ),
            calls,
        )
    }
}
