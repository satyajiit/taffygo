// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BookmarkHtmlCodecTest {
    @Test
    fun `decodes nested Netscape folders and entities`() {
        val source = """
            <!DOCTYPE NETSCAPE-Bookmark-file-1>
            <DL><p>
              <DT><H3>Work &AMP; reading</H3>
              <DL><p>
                <DT><A ADD_DATE="1" HREF="https://docs.example.test/a?x=1&amp;y=2">A &lt; B</A>
                <DT><H3>Nested</H3><DL><p>
                  <DT><A HREF='https://news.example.test/'>News</A>
                </DL><p>
              </DL><p>
            </DL><p>
        """.trimIndent()

        val document = requireNotNull(BookmarkHtmlCodec.decode(source))

        assertEquals(2, document.entryCount)
        val work = document.folders.single()
        assertEquals("Work & reading", work.title)
        assertEquals("A < B", work.bookmarks.single().title)
        assertEquals("https://docs.example.test/a?x=1&y=2", work.bookmarks.single().address)
        assertEquals("News", work.folders.single().bookmarks.single().title)
    }

    @Test
    fun `round trip preserves exact address hierarchy and escaped title`() {
        val original = BookmarkTransferDocument(
            bookmarks = listOf(
                BookmarkTransferDocument.Entry("Root's page", "https://root.example.test/#part"),
            ),
            folders = listOf(
                BookmarkTransferDocument.Folder(
                    title = "Research <2026>",
                    bookmarks = listOf(
                        BookmarkTransferDocument.Entry(
                            "Terms & policy",
                            "https://docs.example.test/terms?a=1&b=2",
                        ),
                    ),
                ),
            ),
        )

        val encoded = requireNotNull(BookmarkHtmlCodec.encode(original))
        val decoded = requireNotNull(BookmarkHtmlCodec.decode(encoded))

        assertEquals(original, decoded)
    }

    @Test
    fun `refuses non bookmark and oversized documents`() {
        assertNull(BookmarkHtmlCodec.decode("<html><a href='https://example.test'>page</a></html>"))
        val oversized = "NETSCAPE-Bookmark-file-1" +
            "x".repeat(BookmarkHtmlCodec.MAX_DOCUMENT_CHARS)
        assertNull(BookmarkHtmlCodec.decode(oversized))
    }

    @Test
    fun `malformed and overlong anchors are counted instead of imported`() {
        val source = buildString {
            appendLine("<!DOCTYPE NETSCAPE-Bookmark-file-1>")
            appendLine("<DL><p>")
            appendLine("<DT><A>missing address</A>")
            append("<DT><A HREF=\"")
            append("x".repeat(8_193))
            appendLine("\">too long</A>")
            appendLine("</DL><p>")
        }

        val document = requireNotNull(BookmarkHtmlCodec.decode(source))

        assertTrue(document.bookmarks.isEmpty())
        assertEquals(2, document.rejectedEntries)
    }

    @Test
    fun `refuses export before building an oversized document`() {
        val oversized = BookmarkTransferDocument(
            bookmarks = List(BookmarkHtmlCodec.MAX_ENTRIES) { index ->
                BookmarkTransferDocument.Entry(
                    title = "bookmark-$index",
                    address = "https://example.test/${"x".repeat(512)}?entry=$index",
                )
            },
        )

        assertNull(BookmarkHtmlCodec.encode(oversized))
    }
}
