// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.settings.ClearBrowsingDataUiState
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.browsing_data.BrowsingDataType
import org.chromium.chrome.browser.browsing_data.TimePeriod
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Exact privacy coupling at the Chromium/profile-ledger deletion seam. */
@RunWith(BaseRobolectricTestRunner::class)
class ChromiumClearDataRepositoryTest {
    @Test
    fun historyAlsoClearsTheDerivedTimeLedger() {
        val plan = planClearData(setOf(ClearBrowsingDataUiState.DataClass.HISTORY))

        assertEquals(listOf(BrowsingDataType.HISTORY), plan.chromiumTypes)
        assertTrue(plan.clearTimeOnSites)
    }

    @Test
    fun timeAloneNeverInventsAChromiumDataClass() {
        val plan = planClearData(setOf(ClearBrowsingDataUiState.DataClass.TIME_ON_SITES))

        assertTrue(plan.chromiumTypes.isEmpty())
        assertTrue(plan.clearTimeOnSites)
    }

    @Test
    fun cacheAndCookiesDoNotEraseMeasuredTime() {
        val plan = planClearData(
            setOf(
                ClearBrowsingDataUiState.DataClass.COOKIES,
                ClearBrowsingDataUiState.DataClass.CACHED_FILES,
            ),
        )

        assertEquals(
            setOf(BrowsingDataType.SITE_DATA, BrowsingDataType.CACHE),
            plan.chromiumTypes.toSet(),
        )
        assertFalse(plan.clearTimeOnSites)
    }

    @Test
    fun everyVisibleRangeMapsExactlyToChromium() {
        assertEquals(
            listOf(
                TimePeriod.LAST_HOUR,
                TimePeriod.LAST_DAY,
                TimePeriod.LAST_WEEK,
                TimePeriod.ALL_TIME,
            ),
            ClearBrowsingDataUiState.Range.entries.map { it.toChromiumRange() },
        )
    }
}
