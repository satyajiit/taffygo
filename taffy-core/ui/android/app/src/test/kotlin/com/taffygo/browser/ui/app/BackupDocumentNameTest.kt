// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupDocumentNameTest {
    @Test fun `ordinary Unicode names are accepted without rewriting`() {
        for (name in listOf("बैकअप.taffybackup", "نسخة.taffybackup", "Backup 🐻.taffybackup", "a".repeat(255))) {
            assertTrue(name, isBackupDocumentNameSafe(name))
        }
    }

    @Test fun `unusable bounds whitespace and invalid surrogate names are refused`() {
        for (name in listOf("", " ", " backup", "backup ", "a".repeat(256), "backup\uD800")) {
            assertFalse(name, isBackupDocumentNameSafe(name))
        }
    }

    @Test fun `line control and invisible directional changes are refused`() {
        for (control in listOf('\u0000', '\n', '\u007f', '\u0085', '\u2028', '\u2029', '\u202e', '\u2066', '\u200b')) {
            assertFalse(isBackupDocumentNameSafe("backup${control}file"))
        }
    }
}
