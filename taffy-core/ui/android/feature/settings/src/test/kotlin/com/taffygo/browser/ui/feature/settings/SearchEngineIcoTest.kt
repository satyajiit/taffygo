// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SearchEngineIcoTest {
    @Test
    fun `encoded and decoded allocations are bounded`() {
        assertFalse(searchEngineMarkInputAllowed(0))
        assertTrue(searchEngineMarkInputAllowed(MAX_SEARCH_ENGINE_MARK_BYTES))
        assertFalse(searchEngineMarkInputAllowed(MAX_SEARCH_ENGINE_MARK_BYTES + 1))
        assertTrue(searchEngineMarkDimensionsAllowed(512, 512))
        assertFalse(searchEngineMarkDimensionsAllowed(513, 24))
        assertEquals(1, searchEngineMarkSampleSize(192, 192))
        assertEquals(2, searchEngineMarkSampleSize(384, 384))
        assertEquals(4, searchEngineMarkSampleSize(512, 512))
    }

    @Test
    fun `ICO selection follows image resolution rather than compressed length`() {
        val bytes = ico(
            Entry(width = 32, height = 32, payload = ByteArray(32) { 1 }),
            Entry(width = 128, height = 128, payload = ByteArray(8) { 2 }),
        )

        val selected = requireNotNull(largestIcoPayload(bytes))

        assertEquals(List(8) { 2.toByte() }, bytes.slice(selected))
    }

    @Test
    fun `ICO offsets use unsigned overflow-safe bounds`() {
        val bytes = ico(Entry(width = 32, height = 32, payload = byteArrayOf(1)))
        writeU32(bytes, ICON_DIRECTORY_BYTES + 12, 0xFFFF_FFF0L)

        assertNull(largestIcoPayload(bytes))
    }

    @Test
    fun `ICO directory count is bounded before walking entries`() {
        val bytes = ByteArray(ICON_DIRECTORY_BYTES)
        writeU16(bytes, 2, 1)
        writeU16(bytes, 4, 0xFFFF)

        assertNull(largestIcoPayload(bytes))
    }

    private fun ico(vararg entries: Entry): ByteArray {
        val directoryBytes = ICON_DIRECTORY_BYTES + entries.size * ICON_ENTRY_BYTES
        val bytes = ByteArray(directoryBytes + entries.sumOf { it.payload.size })
        writeU16(bytes, 2, 1)
        writeU16(bytes, 4, entries.size)
        var payloadOffset = directoryBytes
        entries.forEachIndexed { index, entry ->
            val at = ICON_DIRECTORY_BYTES + index * ICON_ENTRY_BYTES
            bytes[at] = entry.width.toByte()
            bytes[at + 1] = entry.height.toByte()
            writeU32(bytes, at + 8, entry.payload.size.toLong())
            writeU32(bytes, at + 12, payloadOffset.toLong())
            entry.payload.copyInto(bytes, payloadOffset)
            payloadOffset += entry.payload.size
        }
        return bytes
    }

    private fun writeU16(bytes: ByteArray, at: Int, value: Int) {
        bytes[at] = value.toByte()
        bytes[at + 1] = (value ushr 8).toByte()
    }

    private fun writeU32(bytes: ByteArray, at: Int, value: Long) {
        repeat(4) { offset -> bytes[at + offset] = (value ushr (offset * 8)).toByte() }
    }

    private data class Entry(val width: Int, val height: Int, val payload: ByteArray)

    private companion object {
        const val ICON_DIRECTORY_BYTES = 6
        const val ICON_ENTRY_BYTES = 16
    }
}
