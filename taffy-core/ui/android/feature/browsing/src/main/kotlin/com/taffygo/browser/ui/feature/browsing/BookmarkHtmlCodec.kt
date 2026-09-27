// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Bounded Netscape bookmark HTML codec used by Chrome, Firefox, Safari, and Edge exports. */
internal object BookmarkHtmlCodec {
    const val MAX_DOCUMENT_CHARS: Int = 2 * 1_024 * 1_024
    const val MAX_ENTRIES: Int = 5_000
    const val MAX_FOLDERS: Int = 512
    const val MAX_DEPTH: Int = 32

    fun decode(source: String): BookmarkTransferDocument? {
        if (source.length > MAX_DOCUMENT_CHARS || !source.contains(MAGIC, ignoreCase = true)) {
            return null
        }
        val parser = Parser(source)
        return parser.parse()
    }

    fun encode(document: BookmarkTransferDocument): String? {
        if (!fitsExportLimits(document)) return null
        return buildString {
        appendLine("<!DOCTYPE NETSCAPE-Bookmark-file-1>")
        appendLine("<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">")
        appendLine("<TITLE>TaffyGo bookmarks</TITLE>")
        appendLine("<H1>TaffyGo bookmarks</H1>")
        appendLine("<DL><p>")
        document.bookmarks.forEach { appendEntry(it, depth = 1) }
        document.folders.forEach { appendFolder(it, depth = 1) }
        appendLine("</DL><p>")
        }
    }

    private fun fitsExportLimits(document: BookmarkTransferDocument): Boolean {
        var estimatedBytes = EXPORT_HEADER_BYTES
        var entryCount = 0
        var folderCount = 0
        val folders = ArrayDeque<FolderAtDepth>()
        document.folders.forEach { folders.addLast(FolderAtDepth(it, depth = 1)) }

        fun includeEntry(entry: BookmarkTransferDocument.Entry, depth: Int): Boolean {
            entryCount++
            if (
                entryCount > MAX_ENTRIES ||
                entry.title.length > MAX_TITLE_CHARS ||
                entry.address.isEmpty() ||
                entry.address.length > MAX_ADDRESS_CHARS
            ) {
                return false
            }
            estimatedBytes += ENTRY_MARKUP_BYTES + (4L * depth) + escapedUtf8Bytes(entry.title) +
                escapedUtf8Bytes(entry.address)
            return estimatedBytes <= MAX_DOCUMENT_CHARS
        }

        if (!document.bookmarks.all { includeEntry(it, depth = 1) }) return false
        while (folders.isNotEmpty()) {
            val current = folders.removeLast()
            folderCount++
            if (
                folderCount > MAX_FOLDERS ||
                current.depth > MAX_DEPTH ||
                current.folder.title.isBlank() ||
                current.folder.title.length > MAX_TITLE_CHARS
            ) {
                return false
            }
            estimatedBytes += FOLDER_MARKUP_BYTES + (12L * current.depth) +
                escapedUtf8Bytes(current.folder.title)
            if (estimatedBytes > MAX_DOCUMENT_CHARS) return false
            if (!current.folder.bookmarks.all { includeEntry(it, current.depth + 1) }) return false
            current.folder.folders.forEach {
                folders.addLast(FolderAtDepth(it, current.depth + 1))
            }
        }
        return true
    }

    private fun escapedUtf8Bytes(value: String): Long {
        var bytes = 0L
        var index = 0
        while (index < value.length) {
            val char = value[index]
            bytes += when (char) {
                '&' -> 5
                '<', '>' -> 4
                '"' -> 6
                '\'' -> 5
                else -> when {
                    char.code <= 0x7F -> 1
                    char.code <= 0x7FF -> 2
                    char.isHighSurrogate() && value.getOrNull(index + 1)?.isLowSurrogate() == true -> {
                        index++
                        4
                    }
                    else -> 3
                }
            }
            index++
        }
        return bytes
    }

    private fun StringBuilder.appendFolder(folder: BookmarkTransferDocument.Folder, depth: Int) {
        val indent = "    ".repeat(depth.coerceAtMost(MAX_DEPTH + 1))
        append(indent).append("<DT><H3>").append(escape(folder.title)).appendLine("</H3>")
        append(indent).appendLine("<DL><p>")
        folder.bookmarks.forEach { appendEntry(it, depth + 1) }
        folder.folders.forEach { appendFolder(it, depth + 1) }
        append(indent).appendLine("</DL><p>")
    }

    private fun StringBuilder.appendEntry(entry: BookmarkTransferDocument.Entry, depth: Int) {
        append("    ".repeat(depth.coerceAtMost(MAX_DEPTH + 1)))
            .append("<DT><A HREF=\"")
            .append(escape(entry.address))
            .append("\">")
            .append(escape(entry.title))
            .appendLine("</A>")
    }

    private class Parser(private val source: String) {
        private val roots = mutableListOf<MutableFolder>()
        private val rootBookmarks = mutableListOf<BookmarkTransferDocument.Entry>()
        private val stack = ArrayDeque<MutableFolder>()
        private var pendingFolderTitle: String? = null
        private var entries = 0
        private var folders = 0
        private var rejected = 0
        private var cursor = 0

        fun parse(): BookmarkTransferDocument {
            while (cursor < source.length) {
                val start = source.indexOf('<', cursor)
                if (start < 0) break
                val end = tagEnd(start + 1)
                if (end < 0) break
                val tag = source.substring(start + 1, end).trim()
                cursor = end + 1
                when (tagName(tag)) {
                    "h3" -> if (!tag.startsWith('/')) readFolderTitle()
                    "dl" -> if (tag.startsWith('/')) closeFolder() else openFolder()
                    "a" -> if (!tag.startsWith('/')) readEntry(tag)
                }
            }
            while (stack.isNotEmpty()) closeFolder()
            return BookmarkTransferDocument(
                bookmarks = rootBookmarks,
                folders = roots.map(MutableFolder::freeze),
                rejectedEntries = rejected,
            )
        }

        private fun readFolderTitle() {
            val closing = source.indexOf("</h3", cursor, ignoreCase = true)
            if (closing < 0) return
            val closeEnd = tagEnd(closing + 1)
            if (closeEnd < 0) return
            val title = cleanText(source.substring(cursor, closing), MAX_TITLE_CHARS)
            pendingFolderTitle = title.takeIf(String::isNotBlank)
            cursor = closeEnd + 1
        }

        private fun openFolder() {
            val title = pendingFolderTitle ?: return
            pendingFolderTitle = null
            if (folders >= MAX_FOLDERS || stack.size >= MAX_DEPTH) {
                rejected++
                return
            }
            folders++
            stack.addLast(MutableFolder(title))
        }

        private fun closeFolder() {
            pendingFolderTitle = null
            val folder = stack.removeLastOrNull() ?: return
            stack.lastOrNull()?.folders?.add(folder) ?: roots.add(folder)
        }

        private fun readEntry(openingTag: String) {
            val closing = source.indexOf("</a", cursor, ignoreCase = true)
            if (closing < 0) {
                rejected++
                return
            }
            val closeEnd = tagEnd(closing + 1)
            if (closeEnd < 0) {
                rejected++
                return
            }
            val address = attribute(openingTag, "href")
                ?.let(::decodeEntities)
                ?.trim()
                ?.takeIf { it.isNotEmpty() && it.length <= MAX_ADDRESS_CHARS }
            val title = cleanText(source.substring(cursor, closing), MAX_TITLE_CHARS)
            cursor = closeEnd + 1
            if (address == null || entries >= MAX_ENTRIES) {
                rejected++
                return
            }
            entries++
            val entry = BookmarkTransferDocument.Entry(
                title = title.ifBlank { address },
                address = address,
            )
            stack.lastOrNull()?.bookmarks?.add(entry) ?: rootBookmarks.add(entry)
        }

        private fun tagEnd(from: Int): Int {
            var quote = '\u0000'
            for (index in from until source.length) {
                val char = source[index]
                if (quote == '\u0000' && (char == '\'' || char == '"')) {
                    quote = char
                } else if (quote == char) {
                    quote = '\u0000'
                } else if (quote == '\u0000' && char == '>') {
                    return index
                }
            }
            return -1
        }
    }

    private class MutableFolder(val title: String) {
        val bookmarks = mutableListOf<BookmarkTransferDocument.Entry>()
        val folders = mutableListOf<MutableFolder>()

        fun freeze(): BookmarkTransferDocument.Folder = BookmarkTransferDocument.Folder(
            title = title,
            bookmarks = bookmarks,
            folders = folders.map(MutableFolder::freeze),
        )
    }

    private fun tagName(tag: String): String {
        val start = if (tag.startsWith('/')) 1 else 0
        var end = start
        while (end < tag.length && tag[end].isLetterOrDigit()) end++
        return tag.substring(start, end).lowercase()
    }

    private fun attribute(tag: String, wanted: String): String? {
        var cursor = tagName(tag).length
        while (cursor < tag.length) {
            while (cursor < tag.length && tag[cursor].isWhitespace()) cursor++
            val nameStart = cursor
            while (cursor < tag.length && (tag[cursor].isLetterOrDigit() || tag[cursor] in "_:-")) {
                cursor++
            }
            if (cursor == nameStart) {
                cursor++
                continue
            }
            val name = tag.substring(nameStart, cursor)
            while (cursor < tag.length && tag[cursor].isWhitespace()) cursor++
            if (cursor >= tag.length || tag[cursor] != '=') continue
            cursor++
            while (cursor < tag.length && tag[cursor].isWhitespace()) cursor++
            if (cursor >= tag.length) return null
            val quote = tag[cursor].takeIf { it == '\'' || it == '"' }
            if (quote != null) cursor++
            val valueStart = cursor
            while (cursor < tag.length && if (quote == null) !tag[cursor].isWhitespace() else tag[cursor] != quote) {
                cursor++
            }
            val value = tag.substring(valueStart, cursor)
            if (quote != null && cursor < tag.length) cursor++
            if (name.equals(wanted, ignoreCase = true)) return value
        }
        return null
    }

    private fun cleanText(value: String, limit: Int): String = decodeEntities(
        value.replace(TAG_PATTERN, "").trim(),
    ).take(limit)

    private fun escape(value: String): String = buildString(value.length) {
        value.forEach { char ->
            append(
                when (char) {
                    '&' -> "&amp;"
                    '<' -> "&lt;"
                    '>' -> "&gt;"
                    '"' -> "&quot;"
                    '\'' -> "&#39;"
                    else -> char
                },
            )
        }
    }

    private fun decodeEntities(value: String): String = ENTITY_PATTERN.replace(value) { match ->
        val body = match.groupValues[1]
        when (body.lowercase()) {
            "amp" -> "&"
            "lt" -> "<"
            "gt" -> ">"
            "quot" -> "\""
            "apos", "#39" -> "'"
            else -> decodeNumericEntity(body) ?: match.value
        }
    }

    private fun decodeNumericEntity(body: String): String? {
        if (!body.startsWith('#') || body.length < 2) return null
        val hexadecimal = body.getOrNull(1)?.lowercaseChar() == 'x'
        val digits = body.drop(if (hexadecimal) 2 else 1)
        val codePoint = digits.toIntOrNull(if (hexadecimal) 16 else 10) ?: return null
        if (!Character.isValidCodePoint(codePoint) || codePoint in 0xD800..0xDFFF) return null
        return String(Character.toChars(codePoint))
    }

    private const val MAGIC = "NETSCAPE-Bookmark-file-1"
    private const val MAX_TITLE_CHARS = 512
    private const val MAX_ADDRESS_CHARS = 8_192
    private const val EXPORT_HEADER_BYTES = 256L
    private const val ENTRY_MARKUP_BYTES = 32L
    private const val FOLDER_MARKUP_BYTES = 48L
    private val TAG_PATTERN = Regex("<[^>]*>")
    private val ENTITY_PATTERN = Regex("&(#(?:[xX][0-9a-fA-F]+|[0-9]+)|amp|lt|gt|quot|apos);", RegexOption.IGNORE_CASE)

    private data class FolderAtDepth(
        val folder: BookmarkTransferDocument.Folder,
        val depth: Int,
    )
}
