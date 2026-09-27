// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupReducerTest {
    @Test fun `each supported family can be explicitly selected and removed`() {
        val empty = BackupUiState(resumed = true)
        for (content in BackupWindowHost.ContentClass.entries) {
            val selected = reduceBackup(empty, BackupIntent.Toggle(content))
            assertEquals(setOf(content), selected.selection)
            assertTrue(selected.canCreate)
            assertEquals(empty, reduceBackup(selected, BackupIntent.Toggle(content)))
        }
    }

    @Test fun `the screen offers no restore, because a restore creates a second profile`() {
        // Decision 0255 and OD-132: Chromium on Android builds only its first
        // profile, so a restore into a new one would abort the browser.
        val ready = BackupUiState(resumed = true)
        assertFalse(ready.restoreOffered)
        for (content in BackupWindowHost.ContentClass.entries) {
            assertFalse(reduceBackup(ready, BackupIntent.Toggle(content)).restoreOffered)
        }
    }

    @Test fun `selection cannot change while an operation or key ceremony is held`() {
        for (step in BackupUiState.Step.entries.filter { it != BackupUiState.Step.IDLE }) {
            val held = BackupUiState(step = step, resumed = true)
            assertEquals(held, reduceBackup(held, BackupIntent.Toggle(BackupWindowHost.ContentClass.LIBRARY)))
            assertFalse(held.canCreate)
            assertFalse(held.canCheck)
        }
    }

    @Test fun `paused screen cannot change selection or start an operation`() {
        val paused = BackupUiState(selection = BackupWindowHost.ContentClass.entries.toSet())
        assertEquals(paused, reduceBackup(paused, BackupIntent.Toggle(BackupWindowHost.ContentClass.LIBRARY)))
        assertFalse(paused.canCreate)
        assertFalse(paused.canCheck)
    }

    @Test fun `file verification is not described as restore completion`() {
        assertEquals(BackupUiState.Notice.CHECKED_ONLY, backupImportNotice(BackupDocumentImport.ImportStatus.VERIFIED))
        assertEquals(BackupUiState.Notice.IMPORT_REFUSED, backupImportNotice(BackupDocumentImport.ImportStatus.REFUSED))
        assertEquals(BackupUiState.Notice.UNAVAILABLE, backupImportNotice(BackupDocumentImport.ImportStatus.UNAVAILABLE))
    }

    @Test fun `only authenticated saved readback projects a saved backup`() {
        assertEquals(BackupUiState.Notice.SAVED, backupWriteNotice(BackupDocumentTransfer.WriteResult.VERIFIED))
        assertEquals(BackupUiState.Notice.INCOMPLETE, backupWriteNotice(BackupDocumentTransfer.WriteResult.INCOMPLETE))
        assertEquals(BackupUiState.Notice.UNAVAILABLE, backupWriteNotice(BackupDocumentTransfer.WriteResult.UNAVAILABLE))
    }

    @Test fun `dismissing a notice grants no operation and preserves choices`() {
        val state = BackupUiState(selection = setOf(BackupWindowHost.ContentClass.MEMORY), notice = BackupUiState.Notice.INCOMPLETE)
        val dismissed = reduceBackup(state, BackupIntent.DismissNotice)
        assertEquals(state.copy(notice = BackupUiState.Notice.NONE), dismissed)
        assertFalse(dismissed.busy)
    }

    @Test fun `restore name is bounded editable only before native review and grants no authority`() {
        val imported = BackupUiState(resumed = true, step = BackupUiState.Step.RESTORE_IMPORTED)
        val named = reduceBackup(imported, BackupIntent.RestoreNameChanged("Another restored profile"))
        assertEquals("Another restored profile", named.restoreName)
        assertEquals(BackupUiState.Step.RESTORE_IMPORTED, named.step)
        assertEquals(null, named.restore)
        assertEquals(named, reduceBackup(named, BackupIntent.RestoreNameChanged("x".repeat(41))))
        val reviewing = named.copy(step = BackupUiState.Step.RESTORING)
        assertEquals(reviewing, reduceBackup(reviewing, BackupIntent.RestoreNameChanged("Changed")))
        val paused = named.copy(resumed = false)
        assertEquals(paused, reduceBackup(paused, BackupIntent.RestoreNameChanged("Changed")))
    }

    @Test fun `restore name feedback excludes blank oversized and control bearing labels`() {
        for (name in listOf("", " ", " x", "x ", "x\ny", "x\u007fy", "x".repeat(41))) {
            assertFalse(isBackupRestoreNameReady(name))
        }
        assertTrue(isBackupRestoreNameReady("x".repeat(40)))
        assertTrue(isBackupRestoreNameReady("बहाल की गई प्रोफ़ाइल"))
    }
}
