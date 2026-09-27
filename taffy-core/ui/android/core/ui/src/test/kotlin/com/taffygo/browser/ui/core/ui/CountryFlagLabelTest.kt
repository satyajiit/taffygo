// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The fallback that runs whenever [CountryFlagSource] has no artwork.
 */
class CountryFlagLabelTest {

    @Test
    fun `country codes render as local regional indicator flags`() {
        assertEquals("🇮🇳", countryFlagLabel(" in "))
    }

    @Test
    fun `unknown codes use a short local fallback`() {
        assertEquals("UN", countryFlagLabel("unknown"))
        assertEquals("", countryFlagLabel(""))
        assertNull(normalizedFlagCode("unknown"))
    }
}
