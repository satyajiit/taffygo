// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Owns one in-flight site-blocking change and its refusal, independently of
 * SCR-101.
 *
 * The seam returns a Boolean that exists to report a refusal — a host that is
 * empty, over the browser's bound, or carrying a scheme or a path — and both
 * callers used to discard it, so a switch could appear to move while nothing
 * had been recorded. Modelled line for line on [SitePermissionResetController],
 * which already had the shape.
 *
 * `SUCCEEDED` is deliberately silent on the sheet: the switch has moved, and
 * saying so a second time is noise.
 */
internal class SiteBlockingController(
    private val browser: BrowserRepository,
) {
    private val mutableState = MutableStateFlow(SiteBlockingPresentation())
    val state: StateFlow<SiteBlockingPresentation> = mutableState.asStateFlow()
    private var generation = 0L

    fun clear() {
        generation += 1L
        mutableState.value = SiteBlockingPresentation()
    }

    /**
     * Records the change on the plane the selected tab belongs to.
     *
     * [isStillCurrent] is asked after the write, because the person may have
     * navigated or switched tabs while it ran and a result about a page they
     * have left is not a result they should read.
     */
    suspend fun set(host: String, blocked: Boolean, isStillCurrent: () -> Boolean) {
        if (
            host.isBlank() ||
            mutableState.value.status == SiteFilteringUiState.ActionProgress.RUNNING
        ) {
            return
        }
        val request = ++generation
        mutableState.value = SiteBlockingPresentation(
            host = host,
            status = SiteFilteringUiState.ActionProgress.RUNNING,
        )
        val recorded = try {
            browser.setSiteFilteringException(
                host,
                allow = !blocked,
                plane = SiteFilteringPlane.SELECTED_TAB,
            )
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: RuntimeException) {
            false
        }
        if (request != generation) return
        if (!isStillCurrent()) {
            clear()
            return
        }
        mutableState.value = SiteBlockingPresentation(
            host = host,
            status = if (recorded) {
                SiteFilteringUiState.ActionProgress.SUCCEEDED
            } else {
                SiteFilteringUiState.ActionProgress.FAILED
            },
        )
    }
}

/** What [SiteBlockingController] holds: one host, and how its change went. */
internal data class SiteBlockingPresentation(
    val host: String = "",
    val status: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
)
