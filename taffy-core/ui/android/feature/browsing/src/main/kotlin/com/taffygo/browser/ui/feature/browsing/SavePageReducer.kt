// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch

/** Project the page in front of the person onto the save-page sheet. */
internal fun projectSavePage(
    title: String,
    host: String,
    canonicalUrl: String,
    isPrivate: Boolean,
    canSave: Boolean,
    loading: Boolean = false,
    selectedFolderId: String = "",
    saveStatus: SavePageUiState.SaveStatus = SavePageUiState.SaveStatus.IDLE,
): SavePageUiState = SavePageUiState(
    title = title,
    host = host,
    canonicalUrl = canonicalUrl,
    isPrivate = isPrivate,
    canSave = canSave,
    loading = loading,
    selectedFolderId = selectedFolderId,
    saveStatus = saveStatus,
)

/** What the sheet shows after one action. Dismiss is chrome on SCR-101. */
internal fun reduceSavePage(
    state: SavePageUiState,
    intent: SavePageIntent,
): SavePageUiState = when (intent) {
    is SavePageIntent.ChooseFolder -> state.copy(
        selectedFolderId = intent.folderId,
        saveStatus = SavePageUiState.SaveStatus.IDLE,
    )
    SavePageIntent.Save,
    SavePageIntent.Dismiss,
    -> state
}

/** Owns one save-page sheet session and rejects results from every older one. */
internal class SavePageController(
    private val browser: BrowserRepository,
    private val writer: BookmarksWriter,
    private val scope: CoroutineScope,
) {
    val open = MutableStateFlow(false)
    val status = MutableStateFlow(SavePageUiState.SaveStatus.IDLE)

    private var generation = 0L
    private var tabId: TabId? = null
    private var canonicalUrl = ""

    init {
        browser.navigation
            .onEach { closeIfPageChanged(selectedTabId(), it.canonicalUrl) }
            .launchIn(scope)
        browser.tabs
            .onEach { tabs ->
                closeIfPageChanged(
                    tabs.firstOrNull { it.isSelected }?.id,
                    browser.navigation.value.canonicalUrl,
                )
            }
            .launchIn(scope)
    }

    fun begin() {
        generation += 1
        tabId = selectedTabId()
        canonicalUrl = browser.navigation.value.canonicalUrl
        status.value = SavePageUiState.SaveStatus.IDLE
        open.value = true
    }

    fun close() {
        generation += 1
        open.value = false
        status.value = SavePageUiState.SaveStatus.IDLE
        tabId = null
        canonicalUrl = ""
    }

    fun confirm(state: SavePageUiState) {
        val request = start(state) ?: return
        scope.launch {
            val saved = write(state)
            finish(request, saved)
        }
    }

    private fun start(state: SavePageUiState): Long? {
        if (
            !open.value ||
            status.value == SavePageUiState.SaveStatus.SAVING ||
            state.canonicalUrl != canonicalUrl ||
            !state.primaryEnabled
        ) {
            return null
        }
        status.value = SavePageUiState.SaveStatus.SAVING
        return ++generation
    }

    private suspend fun write(state: SavePageUiState): Boolean = try {
        writer.save(state.title, state.canonicalUrl, state.selectedFolderId)
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (_: Exception) {
        false
    }

    private fun finish(request: Long, saved: Boolean) {
        if (
            request != generation ||
            !open.value ||
            selectedTabId() != tabId ||
            browser.navigation.value.canonicalUrl != canonicalUrl
        ) {
            return
        }
        if (saved) close() else status.value = SavePageUiState.SaveStatus.FAILED
    }

    private fun closeIfPageChanged(selectedTabId: TabId?, selectedCanonicalUrl: String) {
        if (open.value && (selectedTabId != tabId || selectedCanonicalUrl != canonicalUrl)) close()
    }

    private fun selectedTabId(): TabId? = browser.tabs.value.firstOrNull { it.isSelected }?.id
}
