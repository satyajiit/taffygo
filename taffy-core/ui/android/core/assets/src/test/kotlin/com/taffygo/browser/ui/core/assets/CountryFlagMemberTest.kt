// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.assets

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CountryFlagMemberTest {

    @Test
    fun `iso codes become the pack member the recipe writes`() {
        assertEquals("flags/in.webp", CountryFlagMember.pathFor("IN"))
        assertEquals("flags/gb.webp", CountryFlagMember.pathFor(" gb "))
    }

    @Test
    fun `anything that is not two letters is not a member`() {
        assertNull(CountryFlagMember.pathFor("unknown"))
        assertNull(CountryFlagMember.pathFor("G"))
        assertNull(CountryFlagMember.pathFor("GB-SCT"))
        assertNull(CountryFlagMember.pathFor(""))
    }
}
