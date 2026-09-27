// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import android.content.Context
import android.content.SharedPreferences
import com.taffygo.browser.ui.core.browser.SearchEngine
import com.taffygo.browser.ui.core.browser.SearchEngineCatalog
import com.taffygo.browser.ui.core.browser.SearchEngineId
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import java.io.Closeable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * The chosen engine, kept in this process's shared preferences.
 *
 * Chromium's PrefService is the profile store, and a new pref there needs a
 * registered name in `profile_preferences.cc`. Until that lands, this is the
 * honest write the address bar can read: Google until the person picks
 * another, surviving process death on this device.
 */
internal class SharedPreferencesSearchEngineRepository(
    private val store: SearchEngineSelectionStore,
) : SearchEngineRepository, Closeable {

    constructor(context: Context) : this(SharedPreferencesSearchEngineStore(context))

    private val current = MutableStateFlow(read())
    private val observation = store.observe {
        current.value = read()
    }
    private var closed = false

    override val selectedId: StateFlow<SearchEngineId> = current.asStateFlow()

    override fun listed(regionCode: String): List<SearchEngine> =
        SearchEngineCatalog.listed(regionCode, current.value)

    override fun searchUrl(query: String): String? =
        SearchEngineCatalog.searchUrl(current.value, query)

    override fun taskSearchUrl(query: String): String? =
        SearchEngineCatalog.taskSearchUrl(current.value, query)

    override suspend fun select(id: SearchEngineId) {
        if (closed) return
        store.write(id.wireName)
        // SharedPreferences changes are observed on the main thread. Read back
        // synchronously as well so the window that made the choice cannot use
        // its old engine while that callback is queued.
        current.value = read()
    }

    private fun read(): SearchEngineId =
        SearchEngineId.parse(store.read())

    override fun close() {
        if (closed) return
        closed = true
        observation.close()
    }
}

internal interface SearchEngineSelectionStore {
    fun read(): String

    fun write(wireName: String)

    fun observe(onChanged: () -> Unit): Closeable
}

private class SharedPreferencesSearchEngineStore(context: Context) : SearchEngineSelectionStore {
    private val preferences = context.applicationContext.getSharedPreferences(
        STORE,
        Context.MODE_PRIVATE,
    )

    override fun read(): String = preferences.getString(KEY, null).orEmpty()

    override fun write(wireName: String) {
        preferences.edit().putString(KEY, wireName).apply()
    }

    override fun observe(onChanged: () -> Unit): Closeable {
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == KEY) onChanged()
        }
        preferences.registerOnSharedPreferenceChangeListener(listener)
        return Closeable {
            preferences.unregisterOnSharedPreferenceChangeListener(listener)
        }
    }

    private companion object {
        const val STORE = "taffy_search_engine"
        const val KEY = "selected_id"
    }
}
