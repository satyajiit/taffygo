// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TaskBrowserProjectionTest {
    @Test
    fun onlyExactTaskOwnershipCanPutAPageInTheWorkspace() {
        val current = tab("current", "task-a")
        val unrelated = tab("previous", "task-b")
        val unowned = tab("unowned", null).copy(isTaffyTab = true)
        val ownPage = tab("person-page", "task-a").copy(isTaffyTab = false)
        val state = projectTaskBrowser("task-a", listOf(current, unrelated, unowned, ownPage), emptyMap())

        assertEquals(listOf(current, ownPage), state.tabs)
    }

    @Test
    fun missingTaskIdentityShowsNoPagesEvenWhenTabsAreAlsoUnowned() {
        for (taskId in listOf(null, "", " ")) {
            assertTrue(projectTaskBrowser(taskId, listOf(tab("page", null)), emptyMap()).tabs.isEmpty())
        }
    }

    @Test
    fun privatePagesNeverEnterTheTaskWorkspace() {
        val page = tab("private", "task-a").copy(isPrivate = true)
        assertTrue(projectTaskBrowser("task-a", listOf(page), emptyMap()).tabs.isEmpty())
    }

    @Test
    fun aTaskSwitchWithdrawsThePreviousTasksArtworkAsWellAsItsPages() {
        val first = tab("first", "task-a")
        val second = tab("second", "task-b")
        val artwork = mapOf(first.id to TabArtwork(), second.id to TabArtwork())

        val state = projectTaskBrowser("task-b", listOf(first, second), artwork)

        assertEquals(listOf(second), state.tabs)
        assertEquals(setOf(second.id), state.artwork.keys)
    }

    @Test
    fun duplicatePublicationDoesNotInventAnotherPage() {
        val page = tab("first", "task-a")
        assertEquals(listOf(page), projectTaskBrowser("task-a", listOf(page, page), emptyMap()).tabs)
    }

    @Test
    fun acceptedExistingSourceTabsAppearWithoutChangingTheirCreationIdentity() {
        val selected = tab("selected", null).copy(isTaffyTab = false)
        val sameSite = tab("unselected", null).copy(isTaffyTab = false)
        val private = tab("private", null).copy(isPrivate = true)
        val state = projectTaskBrowser(
            "comparison", listOf(selected, sameSite, private), emptyMap(),
            acceptedSources = setOf(selected.id, private.id),
        )
        assertEquals(listOf(selected), state.tabs)
        assertEquals(null, state.tabs.single().taskId)
        assertEquals(false, state.tabs.single().isTaffyTab)
    }

    @Test
    fun withdrawingSourceMembershipWithdrawsItsArtwork() {
        val source = tab("selected", null)
        val image = mapOf(source.id to TabArtwork())
        assertEquals(listOf(source), projectTaskBrowser("task", listOf(source), image, setOf(source.id)).tabs)
        val withdrawn = projectTaskBrowser("task", listOf(source), image, emptySet())
        assertTrue(withdrawn.tabs.isEmpty())
        assertTrue(withdrawn.artwork.isEmpty())
    }

    private fun tab(id: String, taskId: String?) = Tab(
        id = TabId(id),
        title = id,
        host = "same.example.test",
        isTaffyTab = true,
        taskId = taskId,
    )
}
