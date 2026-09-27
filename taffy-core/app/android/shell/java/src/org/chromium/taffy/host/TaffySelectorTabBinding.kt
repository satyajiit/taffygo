// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import java.io.Closeable
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabRegistrationObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.taffy.browser.TaffyTaskSourceSelectionBridge

/** Window attachment that discovers Tabs without taking ownership of their components. */
internal class TaffySelectorTabBinding(
    private val selector: TabModelSelector,
    private val provider: ChromiumTaffyProfileRuntimeProvider,
    private val taskSources: TaffyTaskSourceSelectionBridge,
) : Closeable,
    TabModelSelectorTabRegistrationObserver.Observer {
    private var closed = false
    private val registration = TabModelSelectorTabRegistrationObserver(selector)
    private val contentObserver = object : TabModelSelectorTabObserver(selector) {
        override fun onContentChanged(tab: Tab) {
            if (closed || tab.isDestroyed) return
            provider.runtime(tab.profile).requireTab(tab)
            if (TaskSourceTabs.entersRegistry(tab)) {
                check(taskSources.registerTab(tab)) {
                    "A changed product tab could not re-enter the task-source registry"
                }
            }
            if (tab === selector.currentTab) {
                check(taskSources.select(TaskSourceTabs.selection(tab))) {
                    "The changed selected tab could not be published"
                }
            }
        }
    }
    init {
        try {
            registration.addObserverAndNotifyExistingTabRegistration(this)
        } catch (failure: Throwable) {
            rollbackConstruction(failure)
        }
    }

    override fun onTabRegistered(tab: Tab) {
        if (closed || tab.isDestroyed) return
        provider.runtime(tab.profile).requireTab(tab)
        if (TaskSourceTabs.entersRegistry(tab)) {
            check(taskSources.registerTab(tab)) {
                "A product TabModel tab could not enter the task-source registry"
            }
        }
        if (tab === selector.currentTab) {
            check(taskSources.select(TaskSourceTabs.selection(tab))) {
                "The selected product tab could not be published"
            }
        }
    }

    override fun onTabUnregistered(tab: Tab) {
        if (TaskSourceTabs.entersRegistry(tab)) taskSources.unregisterTab(tab)
    }

    override fun close() {
        if (closed) return
        closed = true
        var failure: Throwable? = null
        try {
            registration.removeObserver(this)
        } catch (closeFailure: Throwable) {
            failure = closeFailure
        }
        try {
            registration.destroy()
        } catch (closeFailure: Throwable) {
            failure?.addSuppressed(closeFailure) ?: run { failure = closeFailure }
        }
        try {
            contentObserver.destroy()
        } catch (closeFailure: Throwable) {
            failure?.addSuppressed(closeFailure) ?: run { failure = closeFailure }
        }
        failure?.let { throw it }
    }

    private fun rollbackConstruction(failure: Throwable): Nothing {
        try {
            registration.removeObserver(this)
        } catch (closeFailure: Throwable) {
            failure.addSuppressed(closeFailure)
        }
        try {
            registration.destroy()
        } catch (closeFailure: Throwable) {
            failure.addSuppressed(closeFailure)
        }
        try {
            contentObserver.destroy()
        } catch (closeFailure: Throwable) {
            failure.addSuppressed(closeFailure)
        }
        throw failure
    }
}
