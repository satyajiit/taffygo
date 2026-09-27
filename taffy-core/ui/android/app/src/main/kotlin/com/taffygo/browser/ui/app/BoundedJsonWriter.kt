// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Deterministic JSON writer that refuses output before memory or byte bounds are crossed. */
internal class BoundedJsonWriter(
    private val maxCharacters: Int,
    private val maxBytes: Int,
) {
    private data class Level(var hasValue: Boolean)

    private val output = StringBuilder()
    private val levels = ArrayDeque<Level>()

    fun objectValue(block: BoundedJsonWriter.() -> Unit) {
        append('{')
        levels.addLast(Level(false))
        block()
        levels.removeLast()
        append('}')
    }

    fun field(name: String, value: String?) = fieldValue(name) {
        if (value == null) appendLiteral("null") else appendString(value)
    }

    fun field(name: String, value: Boolean) = fieldValue(name) {
        appendLiteral(if (value) "true" else "false")
    }

    fun field(name: String, value: Number) = fieldValue(name) {
        appendLiteral(value.toString())
    }

    fun fieldNumber(name: String, value: Number?) = fieldValue(name) {
        appendLiteral(value?.toString() ?: "null")
    }

    fun field(name: String, value: ULong) = fieldValue(name) {
        appendLiteral(value.toString())
    }

    fun field(name: String, value: UInt) = fieldValue(name) {
        appendLiteral(value.toString())
    }

    fun fieldObject(name: String, block: BoundedJsonWriter.() -> Unit) = fieldValue(name) {
        objectValue(block)
    }

    fun <T> fieldArray(
        name: String,
        values: Iterable<T>,
        write: BoundedJsonWriter.(T) -> Unit,
    ) = fieldValue(name) {
        append('[')
        var first = true
        values.forEach { value ->
            if (!first) append(',')
            first = false
            write(value)
        }
        append(']')
    }

    fun bytes(): ByteArray {
        val bytes = output.toString().encodeToByteArray()
        if (bytes.size > maxBytes) throw SizeLimitExceeded()
        return bytes
    }

    private inline fun fieldValue(name: String, write: () -> Unit) {
        val level = levels.lastOrNull() ?: error("A JSON field requires an object")
        if (level.hasValue) append(',')
        level.hasValue = true
        appendString(name)
        append(':')
        write()
    }

    private fun appendString(value: String) {
        append('"')
        var index = 0
        while (index < value.length) {
            val character = value[index]
            when (character) {
                '"' -> appendLiteral("\\\"")
                '\\' -> appendLiteral("\\\\")
                '\b' -> appendLiteral("\\b")
                '\u000C' -> appendLiteral("\\f")
                '\n' -> appendLiteral("\\n")
                '\r' -> appendLiteral("\\r")
                '\t' -> appendLiteral("\\t")
                else -> when {
                    character < ' ' -> appendUnicodeEscape(character)
                    character.isHighSurrogate() -> {
                        val low = value.getOrNull(index + 1)
                        if (low?.isLowSurrogate() == true) {
                            append(character)
                            append(low)
                            index++
                        } else {
                            appendUnicodeEscape(character)
                        }
                    }
                    character.isLowSurrogate() -> appendUnicodeEscape(character)
                    else -> append(character)
                }
            }
            index++
        }
        append('"')
    }

    private fun appendUnicodeEscape(character: Char) {
        appendLiteral("\\u")
        repeat(4) { shift ->
            append(HEX[(character.code ushr ((3 - shift) * 4)) and 0x0f])
        }
    }

    private fun appendLiteral(value: String) {
        ensureCapacity(value.length)
        output.append(value)
    }

    private fun append(value: Char) {
        ensureCapacity(1)
        output.append(value)
    }

    private fun ensureCapacity(additional: Int) {
        if (additional > maxCharacters - output.length) throw SizeLimitExceeded()
    }

    internal class SizeLimitExceeded : RuntimeException()

    private companion object {
        const val HEX = "0123456789abcdef"
    }
}
