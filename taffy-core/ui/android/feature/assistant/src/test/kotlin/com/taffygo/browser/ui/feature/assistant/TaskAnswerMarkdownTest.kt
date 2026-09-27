// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TaskAnswerMarkdownTest {
    @Test fun publicSummaryFormatsHeadingsEmphasisAndPrices() {
        val blocks = parse("## Summary: Cedar Phone at Harbor\n\n**Source 1**\n\n- Price: **$329.00**\n- Storage: 128 GB")
        assertEquals(TaskAnswerMarkdown.Kind.HEADING, blocks[0].kind)
        assertEquals("Summary: Cedar Phone at Harbor", blocks[0].text.text)
        assertEquals(2, blocks[0].level)
        assertEquals("Source 1", blocks[1].text.text)
        assertEquals(TaskAnswerMarkdown.Kind.LIST_ITEM, blocks[2].kind)
        assertEquals("•", blocks[2].marker)
        val price = blocks[2].children.single().text
        assertEquals("Price: $329.00", price.text)
        assertEquals(FontWeight.Bold, price.spanStyles.single().item.fontWeight)
        assertEquals("$329.00", price.text.substring(price.spanStyles.single().start, price.spanStyles.single().end))
    }

    @Test fun everyStreamSegmentBoundaryProducesTheSameDocumentIncludingReferences() {
        val whole = "## Summary\n\n**Source 1**\n\n- Price: **$329.00**\n\n[Shop][source]\n\n[source]: https://example.test/item"
        val expected = parse(whole)
        for (cut in 0..whole.length) assertEquals(expected, parse(whole.substring(0, cut), whole.substring(cut)))
    }

    @Test fun incompleteStreamSyntaxStaysVisibleAndClosingItAddsFormatting() {
        assertEquals("Price: **$329", parse("Price: **$329").single().text.text)
        val closed = parse("Price: **$329**").single().text
        assertEquals("Price: $329", closed.text)
        assertEquals(FontWeight.Bold, closed.spanStyles.single().item.fontWeight)
    }

    @Test fun literalCodeAndNestedListsKeepTheirMeaning() {
        val inline = parse("Use `**literal**` and *care*.").single().text
        assertEquals("Use **literal** and care.", inline.text)
        assertEquals(FontFamily.Monospace, inline.spanStyles[0].item.fontFamily)
        assertEquals(FontStyle.Italic, inline.spanStyles[1].item.fontStyle)
        val code = parse("```kotlin\nval s = \"**text**\"\n", "```\nDone")
        assertEquals(TaskAnswerMarkdown.Kind.CODE, code[0].kind)
        assertEquals("val s = \"**text**\"\n", code[0].text.text)
        assertEquals("Done", code[1].text.text)
        val nested = parse("12. **Verified**\n    - Includes delivery\n\n> Quoted page text")
        assertEquals("12.", nested[0].marker)
        assertEquals("Includes delivery", nested[0].children[1].children.single().text.text)
        assertEquals("Quoted page text", nested[1].children.single().text.text)
    }

    @Test fun comparisonTablesHaveSeparateHeaderAndValueCells() {
        val table = parse("| Store | Price |\n| --- | ---: |\n| Orchard | **$349** |\n| Harbor | $329 |") .single()
        assertEquals(TaskAnswerMarkdown.Kind.TABLE, table.kind)
        assertTrue(table.rows[0].isHeader)
        assertEquals(listOf("Store", "Price"), table.rows[0].cells.map { it.text })
        assertEquals(listOf("Orchard", "$349"), table.rows[1].cells.map { it.text })
        assertEquals(FontWeight.Bold, table.rows[1].cells[1].spanStyles.single().item.fontWeight)
        assertEquals(listOf("Harbor", "$329"), table.rows[2].cells.map { it.text })
    }

    @Test fun linksImagesAndHtmlRemainVisibleWithoutClickableAnnotations() {
        val formatted = parse("[Shop](javascript:alert(1)) <b>visible</b> ![photo](https://example.test/a.png)").single().text
        assertEquals("Shop (javascript:alert(1)) <b>visible</b> photo (https://example.test/a.png)", formatted.text)
        assertTrue(formatted.getStringAnnotations(0, formatted.length).isEmpty())
        val html = "<script>window.location='https://example.test'</script>\n"
        assertEquals(html.removeSuffix("\n"), parse(html).single().text.text)
    }

    @Test fun oversizedTableFallsBackToCompleteLiteralTextAndTheNextAnswerStillFormats() {
        val table = "| Store | Price |\n| --- | --- |\n" + "| Shop | $329 |\n".repeat(2_050)
        val fallback = parse(table).single()
        assertEquals(TaskAnswerMarkdown.Kind.PARAGRAPH, fallback.kind)
        assertEquals(table, fallback.text.text)
        assertEquals("Next answer", parse("**Next answer**").single().text.text)
    }

    @Test fun aWideTableWithinTheCellBudgetKeepsTheCompleteAnswerAsLiteralText() {
        val header = List(2_048) { "Store" }.joinToString(" | ")
        val separator = List(2_048) { "---" }.joinToString(" | ")
        val row = List(2_048) { "$329" }.joinToString(" | ")
        val answer = "## Comparison\n\n| $header |\n| $separator |\n| $row |\n\nNo content lost."
        val fallback = parse(answer).single()
        assertEquals(TaskAnswerMarkdown.Kind.PARAGRAPH, fallback.kind)
        assertEquals(answer, fallback.text.text)
    }

    private fun parse(vararg segments: String) = TaskAnswerMarkdown.parse(segments.toList())
}
