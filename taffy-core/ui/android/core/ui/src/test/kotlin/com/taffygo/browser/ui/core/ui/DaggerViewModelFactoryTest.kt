// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.viewmodel.MutableCreationExtras
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.internal.DestinationViewModelStoreOwner
import javax.inject.Provider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Test

/**
 * The seam a destination's arguments travel through to its screen.
 *
 * The handle a screen reads is filed in the back-stack entry's own store and
 * registry rather than in the window's, which is what lets it end when the
 * destination does. That is a change to where the platform looks, so this
 * asserts the whole path on the host: the entry's creation extras, the
 * platform's own `createSavedStateHandle()`, and the question arriving in the view
 * model that was built from it.
 */
class DaggerViewModelFactoryTest {

    @Test
    fun `a view model is built with the saved state of the destination it is for`() {
        val destination = TaffyDestination.AssistantBar(
            question = "compare two phones",
            shape = TaskTemplate.SUMMARIZE_EVIDENCE,
        )
        val entry = DestinationViewModelStoreOwner(NoWindow(), lifecycleOf = LifecycleRegistry::createUnsafe)
        entry.savedStateFor(destination)
        val factory = DaggerViewModelFactory(
            mapOf(
                RecordingViewModel::class.java to
                    Provider { ViewModelCreator { saved -> RecordingViewModel(saved) } },
            ),
        )
        val extras = MutableCreationExtras(entry.defaultViewModelCreationExtras).apply {
            set(ViewModelProvider.VIEW_MODEL_KEY, destination.route)
        }

        val created = factory.create(RecordingViewModel::class.java, extras)

        assertEquals("compare two phones", created.savedState.get<String>(TaffyDestination.QUESTION))
        assertEquals(
            TaskTemplate.SUMMARIZE_EVIDENCE.label,
            created.savedState.get<String>(TaffyDestination.SHAPE),
        )
    }

    @Test
    fun `the separately keyed Ask composer receives its destination arguments without sharing drafts`() {
        val destination = TaffyDestination.AssistantBar(
            question = "",
            shape = TaskTemplate.COMPARE_PRODUCTS,
            attachedTabIds = listOf("orchard", "harbor"),
        )
        val key = "${destination.route}/ask"
        val entry = DestinationViewModelStoreOwner(NoWindow(), lifecycleOf = LifecycleRegistry::createUnsafe)
        val ordinary = entry.savedStateFor(destination)
        entry.savedStateFor(destination, key)
        val factory = DaggerViewModelFactory(
            mapOf(
                RecordingViewModel::class.java to
                    Provider { ViewModelCreator { saved -> RecordingViewModel(saved) } },
            ),
        )
        val extras = MutableCreationExtras(entry.defaultViewModelCreationExtras).apply {
            set(ViewModelProvider.VIEW_MODEL_KEY, key)
        }

        val composer = factory.create(RecordingViewModel::class.java, extras).savedState

        assertEquals("", composer.get<String>(TaffyDestination.QUESTION))
        assertEquals(TaskTemplate.COMPARE_PRODUCTS.label, composer.get<String>(TaffyDestination.SHAPE))
        assertEquals("orchard,harbor", composer.get<String>(TaffyDestination.ATTACHED_TAB_IDS))
        assertNotSame(ordinary, composer)
        composer[TaffyDestination.QUESTION] = "compare the two phones"
        entry.savedStateFor(destination, key)
        val retained = factory.create(RecordingViewModel::class.java, extras).savedState
        assertEquals("compare the two phones", retained.get<String>(TaffyDestination.QUESTION))
        assertEquals("", ordinary.get<String>(TaffyDestination.QUESTION))
    }

    private class RecordingViewModel(val savedState: SavedStateHandle) : ViewModel()

    private class NoWindow : HasDefaultViewModelProviderFactory {
        override val defaultViewModelProviderFactory: ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {}

        override val defaultViewModelCreationExtras: CreationExtras = CreationExtras.Empty
    }
}
