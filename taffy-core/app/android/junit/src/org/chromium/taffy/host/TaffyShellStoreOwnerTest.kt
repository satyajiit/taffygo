// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import androidx.lifecycle.HasDefaultViewModelProviderFactory
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.CreationExtras
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * A view model lives exactly as long as the window whose factory built it.
 *
 * WHAT THIS SUITE IS FOR. The defect was a light/dark switch closing the
 * browser. Upstream recreates the activity on that switch, Android keeps the
 * activity's view-model store across the recreation, and TaffyGo kept every
 * screen's view model in that store — so the new window was handed the old
 * window's view models, which still held the old window's destroyed browser
 * mediator. Here the two windows share one activity, as they do on a device,
 * and each must get its own view model; closing the first window must clear
 * what it built.
 *
 * WHAT IT CANNOT PROVE. That the activity is recreated on a light/dark switch,
 * or that the new window restores its back stack. Those are Chromium's and the
 * saved-state registry's, and the evidence for them is a run on a device.
 */
@RunWith(BaseRobolectricTestRunner::class)
class TaffyShellStoreOwnerTest {

    private class WindowViewModel(val window: Int) : ViewModel() {
        var cleared = false
            private set

        override fun onCleared() {
            cleared = true
        }
    }

    /** One activity, as both windows see it: its creation extras and nothing else. */
    private val activity = object : HasDefaultViewModelProviderFactory {
        override val defaultViewModelProviderFactory: ViewModelProvider.Factory
            get() = error("The window's factory answers, never the activity's")
        override val defaultViewModelCreationExtras: CreationExtras
            get() = CreationExtras.Empty
    }

    private fun windowFactory(window: Int) = object : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>, extras: CreationExtras): T =
            modelClass.cast(WindowViewModel(window))!!
    }

    private fun TaffyShellStoreOwner.model(): WindowViewModel =
        ViewModelProvider.create(this)[WindowViewModel::class]

    @Test
    fun `a window keeps one view model per key for as long as it is open`() {
        val window = TaffyShellStoreOwner(activity, windowFactory(1))

        val first = window.model()

        assertSame(first, window.model())
        assertEquals(1, first.window)
        assertFalse(first.cleared)
    }

    @Test
    fun `closing the window clears every view model it built`() {
        val window = TaffyShellStoreOwner(activity, windowFactory(1))
        val model = window.model()

        window.close()

        assertTrue(model.cleared)
    }

    /** The defect, as one test: the recreated activity's window must not inherit. */
    @Test
    fun `a recreated window builds its own view models from its own factory`() {
        val before = TaffyShellStoreOwner(activity, windowFactory(1))
        val old = before.model()
        before.close()

        val after = TaffyShellStoreOwner(activity, windowFactory(2))
        val fresh = after.model()

        assertNotSame(old, fresh)
        assertEquals(2, fresh.window)
        assertTrue(old.cleared)
        assertFalse(fresh.cleared)
    }
}
