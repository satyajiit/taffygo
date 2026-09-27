// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class DownloadTextBoundsTest {
    @Test
    fun utf8BoundNeverCutsASurrogatePair() {
        assertEquals("ab😀", boundedDownloadText("ab😀tail", maxBytes = 6))
        assertEquals("ab", boundedDownloadText("ab😀tail", maxBytes = 5))
    }

    @Test
    fun controlsBecomeSpacesBeforeTheyReachTheSurface() {
        assertEquals("one two", boundedDownloadText("one\ntwo", maxBytes = 32))
        assertEquals("one two", boundedDownloadText("one\u202Etwo", maxBytes = 32))
    }

    @Test
    fun malformedSurrogatesBecomeBoundedReplacementCharacters() {
        assertEquals("a\uFFFDb", boundedDownloadText("a\uD800b", maxBytes = 5))
        assertEquals("a", boundedDownloadText("a\uD800b", maxBytes = 3))
        assertTrue(downloadTextFits("a\uD800b", maxBytes = 5))
        assertFalse(downloadTextFits("a\uD800b", maxBytes = 4))
    }

    @Test
    fun fitCheckStopsAtTheExactUtf8Bound() {
        assertTrue(downloadTextFits("ab😀", maxBytes = 6))
        assertFalse(downloadTextFits("ab😀", maxBytes = 5))
    }
}
