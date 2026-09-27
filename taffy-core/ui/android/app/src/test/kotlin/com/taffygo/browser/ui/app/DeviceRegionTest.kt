// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.preferences.DEFAULT_REGION_CODE
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Which country a first-run profile starts in.
 *
 * The ordering is the part with a judgement in it, so it is the part that is
 * tested; reading a `TelephonyManager` needs a device and proves nothing about
 * the rule. What this pins is that a weaker signal never overrides a stronger
 * one, that an unsupported country is passed over rather than accepted, and
 * that the chain always ends somewhere.
 */
class DeviceRegionTest {

    @Test
    fun `the network the handset is on wins over everything behind it`() {
        assertEquals("GB", firstSupportedRegion("gb", "in", "US", "US"))
    }

    @Test
    fun `a missing signal is passed over rather than ending the search`() {
        assertEquals("US", firstSupportedRegion(null, null, "us", null))
        assertEquals("US", firstSupportedRegion("", "  ", "us", null))
    }

    @Test
    fun `a country the product does not carry is not accepted`() {
        // The reason the chain filters rather than just taking the first
        // non-empty answer: pinning a profile to a region with no catalogue
        // behind it is worse than falling through to one that has.
        assertEquals("IN", firstSupportedRegion("ZZ", "!!", "in", null))
    }

    @Test
    fun `a device that says nothing usable still lands somewhere`() {
        assertEquals(DEFAULT_REGION_CODE, firstSupportedRegion(null, null, null, null))
        assertEquals(DEFAULT_REGION_CODE, firstSupportedRegion())
    }
}
