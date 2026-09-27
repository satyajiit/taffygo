// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TaskTemplate

/**
 * One box, four readings (UX spec section 5).
 *
 * The order matters and is the specification's: a location wins over
 * everything, then a request for work, then a question, then a search. A search
 * is the fallback because it is the reading that runs immediately and cannot
 * surprise anyone.
 */
internal object AddressBarResolver {

    private val NAVIGABLE_SCHEMES = arrayOf("http", "https", "ftp", "ws", "wss")

    /** What typed input means. Exactly one reading, always. */
    fun resolve(input: String): AddressBarInterpretation {
        val trimmed = input.trim()
        if (trimmed.isEmpty()) return AddressBarInterpretation.Search(trimmed)
        val locationHost = locationHost(trimmed)
        if (locationHost != null) {
            return AddressBarInterpretation.GoTo(trimmed, locationHost)
        }
        val template = templateFor(trimmed)
        return when {
            template != null -> AddressBarInterpretation.TaskForTaffy(trimmed, template)
            looksLikeAQuestion(trimmed) -> AddressBarInterpretation.AskTaffy(trimmed)
            else -> AddressBarInterpretation.Search(trimmed)
        }
    }

    /** The other readings the same input could be forced into. */
    fun alternatives(input: String): List<AddressBarInterpretation> {
        val trimmed = input.trim()
        if (trimmed.isEmpty()) return emptyList()
        val chosen = resolve(trimmed)
        return listOf(
            AddressBarInterpretation.Search(trimmed),
            AddressBarInterpretation.AskTaffy(trimmed),
        ).filter { it::class != chosen::class }
    }

    /**
     * Returns the host only when the text is safe to hand to Chromium as a location.
     *
     * This is intentionally a classifier rather than a URL fixer. Chromium remains
     * the parser at navigation time, but active-content, external and unknown
     * schemes must be stopped before a [GoTo] can be committed. The earlier
     * contains-a-dot rule admitted `javascript:` whenever its body contained a
     * dotted name, and it also silently treated embedded credentials as an address.
     */
    private fun locationHost(input: String): String? {
        if (input.any { it.isWhitespace() || it.code >= 0x80 }) return null

        val schemeEnd = explicitSchemeEnd(input)
        if (schemeEnd != null) {
            val navigable = NAVIGABLE_SCHEMES.any { scheme ->
                input.regionMatches(0, scheme, 0, scheme.length, ignoreCase = true) &&
                    schemeEnd == scheme.length
            }
            if (!navigable || !input.regionMatches(schemeEnd, "://", 0, 3)) return null
            return authorityHost(input, schemeEnd + 3, allowSingleLabel = true)
        }

        val host = authorityHost(input, 0, allowSingleLabel = false) ?: return null
        if (host.startsWith('[')) return host
        if (isIpv4(host)) return host
        if (host.all { it.isDigit() || it == '.' }) return null
        return host.takeIf { it.contains('.') && isValidDnsHost(it) }
    }

    /** A scheme colon, except when the colon introduces an ordinary numeric port. */
    private fun explicitSchemeEnd(input: String): Int? {
        val colon = input.indexOf(':')
        if (colon <= 0 || !input[0].isAsciiLetter()) return null
        for (index in 1 until colon) {
            val character = input[index]
            if (!character.isAsciiLetterOrDigit() && character !in "+-.") return null
        }
        val portEnd = input.indexOfAny(charArrayOf('/', '?', '#'), colon + 1)
            .takeIf { it >= 0 } ?: input.length
        val afterColon = input.substring(colon + 1, portEnd)
        if (afterColon.isNotEmpty() && afterColon.all(Char::isDigit)) return null
        return colon
    }

    private fun authorityHost(input: String, start: Int, allowSingleLabel: Boolean): String? {
        if (start >= input.length) return null
        val end = input.indexOfAny(charArrayOf('/', '?', '#'), start)
            .takeIf { it >= 0 } ?: input.length
        if (end <= start) return null
        val authority = input.substring(start, end)
        if ('@' in authority) return null

        if (authority.startsWith('[')) {
            val close = authority.indexOf(']')
            if (close <= 1 || !validPortSuffix(authority, close + 1)) return null
            val address = authority.substring(1, close)
            val invalidAddress = address.any {
                !it.isDigit() && it.lowercaseChar() !in 'a'..'f' && it != ':' && it != '.'
            }
            if (!address.contains(':') || invalidAddress) {
                return null
            }
            return authority.substring(0, close + 1).lowercase()
        }

        val colon = authority.lastIndexOf(':')
        val host = if (colon >= 0) {
            if (!validPortSuffix(authority, colon)) return null
            authority.substring(0, colon)
        } else {
            authority
        }
        if (host.isEmpty() || !isValidDnsHost(host)) return null
        if (!allowSingleLabel && !host.contains('.') && !isIpv4(host)) return null
        return host.lowercase()
    }

    private fun validPortSuffix(authority: String, separator: Int): Boolean {
        if (separator == authority.length) return true
        if (authority.getOrNull(separator) != ':') return false
        val port = authority.substring(separator + 1)
        return port.isNotEmpty() && port.all(Char::isDigit) && (port.toIntOrNull() ?: -1) in 0..65_535
    }

    private fun isValidDnsHost(host: String): Boolean {
        val effectiveLength = if (host.endsWith('.')) host.length - 1 else host.length
        if (effectiveLength <= 0 || effectiveLength > 253) return false
        var labelLength = 0
        var labelStartsWithHyphen = false
        for (index in 0 until effectiveLength) {
            val character = host[index]
            if (character == '.') {
                if (labelLength == 0 || labelLength > 63 || host[index - 1] == '-' || labelStartsWithHyphen) {
                    return false
                }
                labelLength = 0
                labelStartsWithHyphen = false
            } else {
                if (!character.isAsciiLetterOrDigit() && character != '-') return false
                if (labelLength == 0) labelStartsWithHyphen = character == '-'
                labelLength += 1
            }
        }
        return labelLength in 1..63 && host[effectiveLength - 1] != '-' && !labelStartsWithHyphen
    }

    private fun isIpv4(host: String): Boolean {
        var segments = 0
        var digits = 0
        var value = 0
        for (character in host) {
            if (character == '.') {
                if (digits == 0 || value > 255) return false
                segments += 1
                digits = 0
                value = 0
            } else if (character.isDigit()) {
                digits += 1
                if (digits > 3) return false
                value = value * 10 + character.digitToInt()
            } else {
                return false
            }
        }
        return segments == 3 && digits > 0 && value <= 255
    }

    private fun Char.isAsciiLetter(): Boolean = this in 'a'..'z' || this in 'A'..'Z'

    private fun Char.isAsciiLetterOrDigit(): Boolean = isAsciiLetter() || this in '0'..'9'

    private fun looksLikeAQuestion(input: String): Boolean =
        input.endsWith('?') || startsWithAny(input, QUESTION_OPENERS)

    private fun templateFor(input: String): TaskTemplate? = when {
        startsWithAny(input, COMPARISON_OPENERS) ||
            startsWithAny(input, PRICE_COMPARISON_OPENERS, terminalQuestion = true) ->
            TaskTemplate.COMPARE_PRODUCTS
        // These are deliberately checked before the broad table verb
        // "extract". They ask for prose or document evidence, not rows, and
        // an unreachable longer opener is a silent routing defect.
        startsWithAny(input, RESEARCH_PRIORITY_OPENERS) ->
            TaskTemplate.SUMMARIZE_EVIDENCE
        startsWithAny(input, TABLE_OPENERS) ->
            TaskTemplate.BUILD_A_SOURCE_TABLE
        startsWithAny(input, RESEARCH_OPENERS) -> TaskTemplate.SUMMARIZE_EVIDENCE
        startsWithAny(input, ERRAND_OPENERS) || isPriceRequest(input) -> TaskTemplate.WEB_ERRAND
        else -> null
    }

    // A price word alone is still a search or question. The person must ask
    // to find an offer before the errand's discovery consent is presented.
    private fun isPriceRequest(input: String): Boolean =
        startsWithAny(input, PRICE_REQUEST_OPENERS) &&
            !startsWithAny(input, FIND_EXPLANATION_OPENERS) && PRICE_WORD.containsMatchIn(input)

    /** Case-insensitive prefix matching without allocating a vararg or joined prefix per keystroke. */
    private fun startsWithAny(
        input: String,
        openers: Array<String>,
        terminalQuestion: Boolean = false,
    ) = openers.any { opener ->
        input.length >= opener.length &&
            input.regionMatches(0, opener, 0, opener.length, ignoreCase = true) &&
            (input.length == opener.length || input[opener.length].isWhitespace() ||
                (terminalQuestion && input.length == opener.length + 1 && input.last() == '?'))
    }

    private val PRICE_COMPARISON_OPENERS = arrayOf(
        "which is cheaper",
        "which is cheapest",
        "which costs less",
        "which of these is cheaper",
    )

    private val PRICE_REQUEST_OPENERS = arrayOf(
        "where can i get",
        "where can i buy",
        "where can i find",
        "find",
    )

    private val FIND_EXPLANATION_OPENERS = arrayOf("find out")
    private val PRICE_WORD = Regex("\\b(?:cheaper|cheapest)\\b", RegexOption.IGNORE_CASE)

    private val COMPARISON_OPENERS = arrayOf(
        "compare",
        "contrast",
        "choose between",
        "pick between",
        "rank these",
        "which is better",
        "which should i choose",
        "which should i buy",
        "shortlist",
    )

    private val TABLE_OPENERS = arrayOf(
        "build a table",
        "build a source table",
        "make a table",
        "create a table",
        "build a spreadsheet",
        "make a spreadsheet",
        "create a spreadsheet",
        "turn this into a spreadsheet",
        "turn these into a spreadsheet",
        "convert to csv",
        "make a csv",
        "create a csv",
        "extract data",
        "extract fields",
        "extract a table",
        "analyze this data",
        "analyse this data",
        "calculate",
        "compute",
        "extract",
        "collect",
        "gather",
        "organize",
        "organise",
    )

    private val RESEARCH_OPENERS = arrayOf(
        "summarize",
        "summarise",
        "research",
        "investigate",
        "analyze",
        "analyse",
        "review",
        "plan",
        "build an itinerary",
        "make an itinerary",
        "create a brief",
        "write a brief",
        "draft a brief",
        "create a report",
        "make a report",
        "write a report",
        "create a presentation",
        "make a presentation",
        "create slides",
        "make slides",
        "translate",
        "rewrite",
        "proofread",
        "analyze this video",
        "analyse this video",
        "analyze this image",
        "analyse this image",
        "explain",
        "outline",
        "transcribe",
        "fact check",
        "verify",
        "extract text",
    )

    private val RESEARCH_PRIORITY_OPENERS = arrayOf(
        "extract text",
        "read text from",
    )

    private val ERRAND_OPENERS = arrayOf(
        "book",
        "buy",
        "order",
        "reserve",
        "schedule",
        "add to cart",
        "fill in",
        "fill out",
        "prefill",
        "apply for",
        "sign up",
        "register for",
        "renew",
        "download",
        "submit",
    )

    private val QUESTION_OPENERS = arrayOf(
        "is", "are", "was", "were", "does", "do", "did", "can", "could", "should", "would",
        "what", "why", "how", "when", "where", "who", "which",
    )
}
