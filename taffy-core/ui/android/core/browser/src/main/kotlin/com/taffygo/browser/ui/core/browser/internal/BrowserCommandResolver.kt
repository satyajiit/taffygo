// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation

/** Exact browser-command vocabulary; arbitrary text remains an ordinary content reading. */
internal object BrowserCommandResolver {
    fun resolve(input: String): AddressBarInterpretation.BrowserCommand? {
        val trimmed = input.trim()
        val definition = DEFINITIONS.firstOrNull {
            it.typedForm.equals(trimmed, ignoreCase = true)
        } ?: return null
        return AddressBarInterpretation.BrowserCommand(trimmed, definition.command)
    }

    fun completions(input: String): List<AddressBarInterpretation.BrowserCommand> {
        val prefix = input.trim()
        if (prefix.length < MIN_PREFIX_CHARS) return emptyList()
        return DEFINITIONS.asSequence()
            .filter { it.typedForm.startsWith(prefix, ignoreCase = true) }
            .map { AddressBarInterpretation.BrowserCommand(it.typedForm, it.command) }
            .toList()
    }

    private data class Definition(
        val command: AddressBarCommand,
        val typedForm: String,
    )

    private const val MIN_PREFIX_CHARS = 2
    private val DEFINITIONS = arrayOf(
        Definition(AddressBarCommand.OPEN_HISTORY, "open history"),
        Definition(AddressBarCommand.OPEN_BOOKMARKS, "open bookmarks"),
        Definition(AddressBarCommand.OPEN_DOWNLOADS, "open downloads"),
        Definition(AddressBarCommand.OPEN_SETTINGS, "open settings"),
        Definition(AddressBarCommand.OPEN_CLEAR_BROWSING_DATA, "clear browsing data"),
    )
}
