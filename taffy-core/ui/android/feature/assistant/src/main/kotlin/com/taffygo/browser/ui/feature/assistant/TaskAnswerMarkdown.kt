// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle
import org.commonmark.ext.gfm.tables.TableBlock
import org.commonmark.ext.gfm.tables.TableHead
import org.commonmark.ext.gfm.tables.TablesExtension
import org.commonmark.node.BlockQuote
import org.commonmark.node.BulletList
import org.commonmark.node.Code
import org.commonmark.node.Emphasis
import org.commonmark.node.FencedCodeBlock
import org.commonmark.node.HardLineBreak
import org.commonmark.node.Heading
import org.commonmark.node.HtmlBlock
import org.commonmark.node.HtmlInline
import org.commonmark.node.Image
import org.commonmark.node.IndentedCodeBlock
import org.commonmark.node.Link
import org.commonmark.node.LinkReferenceDefinition
import org.commonmark.node.Node
import org.commonmark.node.OrderedList
import org.commonmark.node.Paragraph
import org.commonmark.node.SoftLineBreak
import org.commonmark.node.StrongEmphasis
import org.commonmark.node.Text
import org.commonmark.node.ThematicBreak
import org.commonmark.parser.Parser

/** CommonMark owns parsing. This maps its tree to inert, selectable Compose content. */
internal object TaskAnswerMarkdown {
    enum class Kind { PARAGRAPH, HEADING, LIST_ITEM, QUOTE, CODE, TABLE, DIVIDER }

    data class TableRow(val isHeader: Boolean, val cells: List<AnnotatedString>)
    data class Block(
        val kind: Kind,
        val text: AnnotatedString = AnnotatedString(""),
        val level: Int = 0,
        val marker: String = "",
        val children: List<Block> = emptyList(),
        val rows: List<TableRow> = emptyList(),
    )

    private val parser = Parser.builder()
        .maxOpenBlockParsers(48)
        .maxInlineNesting(48)
        .extensions(listOf(TablesExtension.builder().maxCells(4_096).build()))
        .build()

    fun parse(segments: List<String>): List<Block> {
        val text = segments.joinToString("")
        return try {
            children(parser.parse(text)).flatMap(::blocks)
        } catch (_: IllegalArgumentException) {
            // A library resource limit must never hide the answer or terminate its collector.
            listOf(Block(Kind.PARAGRAPH, AnnotatedString(text)))
        }
    }

    private fun blocks(node: Node): List<Block> = when (node) {
        is Heading -> listOf(Block(Kind.HEADING, inline(node), level = node.level))
        is Paragraph -> listOf(Block(Kind.PARAGRAPH, inline(node)))
        is FencedCodeBlock -> listOf(Block(Kind.CODE, AnnotatedString(node.literal), marker = node.info.orEmpty()))
        is IndentedCodeBlock -> listOf(Block(Kind.CODE, AnnotatedString(node.literal)))
        is HtmlBlock -> listOf(Block(Kind.PARAGRAPH, AnnotatedString(node.literal)))
        is BlockQuote -> listOf(Block(Kind.QUOTE, children = children(node).flatMap(::blocks)))
        is BulletList -> children(node).map { Block(Kind.LIST_ITEM, marker = "•", children = children(it).flatMap(::blocks)) }
        is OrderedList -> children(node).mapIndexed { index, item ->
            Block(Kind.LIST_ITEM, marker = "${(node.markerStartNumber ?: 1) + index}.", children = children(item).flatMap(::blocks))
        }
        is TableBlock -> {
            val rows = children(node).flatMap { section ->
                children(section).map { row -> TableRow(section is TableHead, children(row).map(::inline)) }
            }
            // Even a two-row table can exceed Compose's finite width when it has thousands of columns.
            require(rows.all { it.cells.size <= 12 })
            listOf(Block(Kind.TABLE, rows = rows))
        }
        is ThematicBreak -> listOf(Block(Kind.DIVIDER))
        is LinkReferenceDefinition -> emptyList()
        else -> children(node).flatMap(::blocks)
    }

    private fun inline(node: Node): AnnotatedString = buildAnnotatedString {
        fun appendNode(current: Node) {
            when (current) {
                is Text -> append(current.literal)
                is Code -> withStyle(SpanStyle(fontFamily = FontFamily.Monospace)) { append(current.literal) }
                is Emphasis -> withStyle(SpanStyle(fontStyle = FontStyle.Italic)) { children(current).forEach(::appendNode) }
                is StrongEmphasis -> withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { children(current).forEach(::appendNode) }
                is SoftLineBreak -> append(" ")
                is HardLineBreak -> append("\n")
                is HtmlInline -> append(current.literal)
                is Link -> {
                    children(current).forEach(::appendNode)
                    val label = (current.firstChild as? Text)?.takeIf { it.next == null }?.literal
                    if (current.destination != label) append(" (${current.destination})")
                }
                is Image -> {
                    children(current).forEach(::appendNode)
                    append(" (${current.destination})")
                }
                else -> children(current).forEach(::appendNode)
            }
        }
        children(node).forEach(::appendNode)
    }

    private fun children(node: Node): List<Node> = buildList {
        var child = node.firstChild
        while (child != null) {
            add(child)
            child = child.next
        }
    }
}
