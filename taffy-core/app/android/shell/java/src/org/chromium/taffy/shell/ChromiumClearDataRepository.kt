// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.feature.settings.ClearBrowsingDataUiState
import com.taffygo.browser.ui.feature.settings.ClearDataRepository
import java.io.Closeable
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.suspendCancellableCoroutine
import org.chromium.base.ThreadUtils
import org.chromium.chrome.browser.browsing_data.BrowsingDataBridge
import org.chromium.chrome.browser.browsing_data.BrowsingDataType
import org.chromium.chrome.browser.browsing_data.TimePeriod
import org.chromium.chrome.browser.profiles.Profile

/** Clears only the data classes Chromium can prove finished for this profile. */
internal class ChromiumClearDataRepository(
    profile: Profile,
    private val bridge: BrowsingDataBridge = BrowsingDataBridge.getForProfile(profile),
    private val clearTimeOnSites: ((ClearBrowsingDataUiState.Range) -> Boolean)? = null,
) : ClearDataRepository,
    Closeable {
    override val available: Boolean
        get() = !closed

    override val supportedClasses: Set<ClearBrowsingDataUiState.DataClass> = buildSet {
        add(ClearBrowsingDataUiState.DataClass.HISTORY)
        add(ClearBrowsingDataUiState.DataClass.COOKIES)
        add(ClearBrowsingDataUiState.DataClass.CACHED_FILES)
        if (clearTimeOnSites != null) add(ClearBrowsingDataUiState.DataClass.TIME_ON_SITES)
    }

    private var pending: CancellableContinuation<Boolean>? = null
    private var closed = false

    init {
        require(!profile.isOffTheRecord) {
            "Browsing-data settings must be rooted in the original regular profile"
        }
    }

    override suspend fun clear(
        range: ClearBrowsingDataUiState.Range,
        classes: Set<ClearBrowsingDataUiState.DataClass>,
    ): Boolean {
        ThreadUtils.assertOnUiThread()
        if (closed || classes.isEmpty() || !supportedClasses.containsAll(classes)) return false
        if (pending != null) return false
        val plan = planClearData(classes)
        if (plan.chromiumTypes.isEmpty()) {
            return clearTimeOnSites?.invoke(range) == true
        }
        return suspendCancellableCoroutine { continuation ->
            pending = continuation
            continuation.invokeOnCancellation {
                if (pending === continuation) pending = null
            }
            try {
                bridge.clearBrowsingData(
                    {
                        val timeCleared =
                            !plan.clearTimeOnSites || clearTimeOnSites?.invoke(range) == true
                        if (pending === continuation) {
                            pending = null
                            if (continuation.isActive) continuation.resume(!closed && timeCleared)
                        }
                    },
                    plan.chromiumTypes.toIntArray(),
                    range.toChromiumRange(),
                )
            } catch (_: RuntimeException) {
                if (pending === continuation) pending = null
                if (continuation.isActive) continuation.resume(false)
            }
        }
    }

    override fun close() {
        ThreadUtils.assertOnUiThread()
        if (closed) return
        closed = true
        val continuation = pending
        pending = null
        if (continuation?.isActive == true) continuation.resume(false)
    }

}

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun planClearData(
    classes: Set<ClearBrowsingDataUiState.DataClass>,
): ClearDataPlan = ClearDataPlan(
    chromiumTypes = classes.mapNotNull { dataClass ->
        when (dataClass) {
            ClearBrowsingDataUiState.DataClass.HISTORY -> BrowsingDataType.HISTORY
            ClearBrowsingDataUiState.DataClass.COOKIES -> BrowsingDataType.SITE_DATA
            ClearBrowsingDataUiState.DataClass.CACHED_FILES -> BrowsingDataType.CACHE
            ClearBrowsingDataUiState.DataClass.TIME_ON_SITES -> null
        }
    },
    // Time on sites is browser-history-derived privacy data. Choosing History
    // clears it even when its separate row was not selected, so clearing never
    // leaves a shadow visit ledger behind a blank Chromium history screen.
    clearTimeOnSites =
        ClearBrowsingDataUiState.DataClass.HISTORY in classes ||
            ClearBrowsingDataUiState.DataClass.TIME_ON_SITES in classes,
)

@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
fun ClearBrowsingDataUiState.Range.toChromiumRange(): Int = when (this) {
    ClearBrowsingDataUiState.Range.LAST_HOUR -> TimePeriod.LAST_HOUR
    ClearBrowsingDataUiState.Range.LAST_DAY -> TimePeriod.LAST_DAY
    ClearBrowsingDataUiState.Range.LAST_WEEK -> TimePeriod.LAST_WEEK
    ClearBrowsingDataUiState.Range.ALL_TIME -> TimePeriod.ALL_TIME
}
