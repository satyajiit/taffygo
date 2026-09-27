// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidCredentialAdapterTest {
    @Test
    fun `Google nonce accepts exactly lowercase SHA-256 hex`() {
        assertTrue("0123456789abcdef".repeat(4).isValidGoogleNonceHash())
        assertFalse("a".repeat(63).isValidGoogleNonceHash())
        assertFalse("a".repeat(65).isValidGoogleNonceHash())
        assertFalse("A".repeat(64).isValidGoogleNonceHash())
        assertFalse(("a".repeat(63) + "-").isValidGoogleNonceHash())
    }
}
