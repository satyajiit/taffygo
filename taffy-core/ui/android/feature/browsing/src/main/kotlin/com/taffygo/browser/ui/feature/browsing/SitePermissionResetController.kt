// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Owns the confirmation and one in-flight reset independently of SCR-101. */
internal class SitePermissionResetController(
    private val siteInfo: SiteInfoRepository,
) {
    private val mutableState = MutableStateFlow(SitePermissionResetPresentation())
    val state: StateFlow<SitePermissionResetPresentation> = mutableState.asStateFlow()
    private var generation = 0L

    fun request(host: String) {
        if (
            host.isBlank() ||
            mutableState.value.status == SiteFilteringUiState.ActionProgress.RUNNING
        ) {
            return
        }
        generation += 1L
        mutableState.value = SitePermissionResetPresentation(host = host)
    }

    fun clear() {
        generation += 1L
        mutableState.value = SitePermissionResetPresentation()
    }

    suspend fun confirm(host: String, isStillCurrent: () -> Boolean) {
        val current = mutableState.value
        if (
            host.isBlank() ||
            current.host != host ||
            current.status != SiteFilteringUiState.ActionProgress.IDLE
        ) {
            return
        }
        val request = ++generation
        mutableState.value = current.copy(status = SiteFilteringUiState.ActionProgress.RUNNING)
        val result = try {
            siteInfo.resetPermissions(host)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: RuntimeException) {
            SiteInfoRepository.PermissionResetResult.FAILED
        }
        if (request != generation) return
        if (!isStillCurrent()) {
            clear()
            return
        }
        mutableState.value = SitePermissionResetPresentation(
            host = host,
            status = if (result == SiteInfoRepository.PermissionResetResult.APPLIED) {
                SiteFilteringUiState.ActionProgress.SUCCEEDED
            } else {
                SiteFilteringUiState.ActionProgress.FAILED
            },
        )
    }
}

internal data class SitePermissionResetPresentation(
    val host: String = "",
    val status: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
)
