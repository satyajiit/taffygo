// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Save page (SCR-811): private refusal, empty writer, dismiss. */
class SavePageReducerTest {

    @Test
    fun `a writer on a public page enables save`() {
        val state = projectSavePage(
            title = "Retention policy",
            host = "docs.example.test",
            canonicalUrl = "https://docs.example.test/policies/retention?region=in#exceptions",
            isPrivate = false,
            canSave = true,
        )

        assertTrue(state.primaryEnabled)
        assertFalse(state.loading)
    }

    @Test
    fun `an empty writer keeps save disabled`() {
        val state = projectSavePage(
            title = "Retention policy",
            host = "docs.example.test",
            canonicalUrl = "https://docs.example.test/policies/retention",
            isPrivate = false,
            canSave = false,
        )

        assertFalse(state.primaryEnabled)
        assertFalse(state.canSave)
    }

    @Test
    fun `a private tab refuses even when a writer exists`() {
        val state = projectSavePage(
            title = "Price history",
            host = "prices.example.test",
            canonicalUrl = "https://prices.example.test/history",
            isPrivate = true,
            canSave = true,
        )

        assertTrue(state.isPrivate)
        assertFalse(state.primaryEnabled)
    }

    @Test
    fun `choosing a folder is remembered`() {
        val start = projectSavePage(
            title = "Retention policy",
            host = "docs.example.test",
            canonicalUrl = "https://docs.example.test/policies/retention",
            isPrivate = false,
            canSave = true,
        )
        val chosen = reduceSavePage(start, SavePageIntent.ChooseFolder("work"))

        assertEquals("work", chosen.selectedFolderId)
        assertTrue(chosen.primaryEnabled)
    }

    @Test
    fun `save and dismiss leave the sheet's facts alone`() {
        val start = projectSavePage(
            title = "Retention policy",
            host = "docs.example.test",
            canonicalUrl = "https://docs.example.test/policies/retention",
            isPrivate = false,
            canSave = false,
        )

        assertEquals(start, reduceSavePage(start, SavePageIntent.Save))
        assertEquals(start, reduceSavePage(start, SavePageIntent.Dismiss))
    }

    @Test
    fun `a host without an exact canonical url cannot be saved`() {
        val state = projectSavePage(
            title = "Local document",
            host = "docs.example.test",
            canonicalUrl = "",
            isPrivate = false,
            canSave = true,
        )

        assertFalse(state.primaryEnabled)
    }

    @Test
    fun `an in flight save is disabled and a failed save can be retried`() {
        val saving = projectSavePage(
            title = "Retention policy",
            host = "docs.example.test",
            canonicalUrl = "https://docs.example.test/policies/retention",
            isPrivate = false,
            canSave = true,
            saveStatus = SavePageUiState.SaveStatus.SAVING,
        )
        val failed = saving.copy(saveStatus = SavePageUiState.SaveStatus.FAILED)

        assertFalse(saving.primaryEnabled)
        assertTrue(failed.primaryEnabled)
    }
}
