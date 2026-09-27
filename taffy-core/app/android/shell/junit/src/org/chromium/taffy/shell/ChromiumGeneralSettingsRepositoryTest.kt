// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.settings.GeneralSettingsRepository
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.profiles.Profile
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumGeneralSettingsRepositoryTest {
    private val profile = mock(Profile::class.java)

    @Test
    fun `write failure is different from readback failure`() {
        val writeFailure = FakeLocationStore(writeFailure = true)
        val readFailure = FakeLocationStore(readFailure = true)

        assertEquals(
            GeneralSettingsRepository.DownloadLocationChoice.WRITE_FAILED,
            persistAndVerifyDownloadLocation(profile, "/download", writeFailure),
        )
        assertEquals(
            GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED,
            persistAndVerifyDownloadLocation(profile, "/download", readFailure),
        )
    }

    @Test
    fun `mismatched readback is not reported as saved`() {
        val store = FakeLocationStore(readValue = "/somewhere-else")

        assertEquals(
            GeneralSettingsRepository.DownloadLocationChoice.READBACK_FAILED,
            persistAndVerifyDownloadLocation(profile, "/download", store),
        )
    }

    @Test
    fun `only exact readback is reported as saved`() {
        val store = FakeLocationStore(readValue = "/download")

        assertEquals(
            GeneralSettingsRepository.DownloadLocationChoice.SAVED,
            persistAndVerifyDownloadLocation(profile, "/download", store),
        )
    }

    private class FakeLocationStore(
        private val writeFailure: Boolean = false,
        private val readFailure: Boolean = false,
        private val readValue: String = "/download",
    ) : ChromiumDownloadLocationStore {
        override fun write(profile: Profile, path: String) {
            if (writeFailure) throw IllegalStateException("write failed")
        }

        override fun read(profile: Profile): String {
            if (readFailure) throw IllegalStateException("read failed")
            return readValue
        }
    }
}
