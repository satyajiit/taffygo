// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.task.taskStartRequest
import com.taffygo.browser.ui.core.ui.VoiceEntryState

/**
 * The box, wherever it stands: screen SCR-103, the start page's own centred
 * composer, which is typed into where it is rather than opening SCR-103 first,
 * and the Ask overlay's, over a page (decision 0135).
 *
 * [interpretation] is what the typed text resolved to. [reading] is what the box
 * currently *means*, which is not the same thing once a person has stated a
 * shape — see below. One of them is never null while there is input, because "we
 * do not know what this means" is not a supported interpretation (UX spec
 * section 5), and the reading is shown before anything consequential runs.
 *
 * ## A task starts where it was typed
 *
 * A reading that is a job for Taffy no longer leaves the box for a preview
 * screen. [start] is the one start rule's answer for it — what the task would
 * be, or why it cannot be one yet — and the rows under the box say that answer
 * in words before the person sends: the goal, that Taffy will find the site and
 * may open more, and where page text goes (decision 0087 section 4). Sending
 * moves the box through [starting] to [started], and the words stay in the box
 * the whole way, because they are what the panel under it is about.
 */
data class AddressBarUiState(
    /** What the user has typed. */
    val input: String = "",
    /** What the input resolved to on its own, or null while the box is empty. */
    val interpretation: AddressBarInterpretation? = null,
    /** The rows under the box. */
    val suggestions: List<Suggestion> = emptyList(),
    /** Unconfirmed speech is transient and never replaces [input] by itself. */
    val voiceEntry: VoiceEntryState = VoiceEntryState.Closed,
    /**
     * The shape a person chose from the plus, drawn as a chip under the box.
     *
     * It is an option applied to what they are about to say rather than a
     * navigation: choosing one changes what the box means and nothing else
     * happens until they send. Null is the ordinary case — no shape stated, and
     * no chip drawn.
     */
    val shape: TaskTemplate? = null,
    /**
     * The stores the person attached from the plus, drawn as chips under the box.
     *
     * Attached whole, and reaching Taffy as tools it may search (decision
     * 0133): a chip says a store is on this request, and the consent line under
     * the reading says the same in a sentence. Empty is the ordinary case.
     */
    val attachedStores: Set<TaskAttachedStore> = emptySet(),
    /** Whether the plus menu stands open over this box. */
    val menuOpen: Boolean = false,
    /**
     * The pages the question is about, drawn as chips under the box, in the
     * order the person put them there.
     *
     * Only the Ask overlay's box carries any: the page it was opened over,
     * the tabs picked in the switcher, or the ones added from the plus. They
     * are the consent for what Taffy reads — a question over pages is a
     * research shape over exactly those hosts, and a page that has closed
     * stays on the row in caution and refuses the start until it is taken
     * off, because a consent that quietly read one page fewer would be a
     * consent for something else.
     */
    val attachedPages: List<AttachedPage> = emptyList(),
    /** What the open tabs offer the question, on the Ask overlay's box. */
    val pages: AskPagesSnapshot = AskPagesSnapshot(),
    /** Whether the Add pages sheet stands open over this box. */
    val attachOpen: Boolean = false,
    /** The facts the start rule reads that the person did not type. */
    val conditions: StartConditions = StartConditions(),
    /** Whether a start has been sent and the core has not yet answered. */
    val starting: Boolean = false,
    /** The task this box started, once the core has said its name. */
    val started: StartedTask? = null,
    /** Why the last start did not take, said under the box until the next keystroke. */
    val startFailure: TaskStartFailure? = null,
    val savedFlows: SavedFlowRepeatState = SavedFlowRepeatState(),
) {
    /**
     * What the box means now: the shape the person stated, else what their words
     * resolved to on their own.
     *
     * A stated shape wins over the resolver, and that is the point of stating
     * one — a person who picked "Compare products" has said what kind of job
     * this is, and a resolver that reads their words as a search would be
     * overruling them. It is derived rather than written into
     * [interpretation] so that the resolver's own answer survives underneath:
     * clear the chip and the box goes back to meaning what it read, with no
     * second resolve and nothing to keep in step.
     *
     * A stated shape over an empty box means nothing at all, because a task with
     * no goal is not a reading — it is a chip waiting for words.
     */
    val reading: AddressBarInterpretation?
        get() {
            val typed = input.takeIf { it.isNotBlank() }
            val stated = shape
            if (stated != null) return typed?.let { AddressBarInterpretation.TaskForTaffy(it, stated) }
            // Everything typed on the Ask overlay goes to Taffy. Preserve a
            // recognized task; replacing it with a generic question would
            // turn an attached-page comparison or download into a summary.
            // Locations and search terms remain questions on this surface.
            if (conditions.asksInPlace) {
                if (typed == null) return null
                return interpretation as? AddressBarInterpretation.TaskForTaffy
                    ?: AddressBarInterpretation.AskTaffy(typed)
            }
            return interpretation
        }

    /**
     * The start rule's answer for the reading, or null when the reading is not
     * a start at all.
     *
     * On the start page and screen SCR-103, two readings start here. A task
     * for Taffy in the errand shape names no page: Taffy finds the site
     * itself, under the discovery consent the rows under the box show. A
     * question on a blank tab is the same job wearing the resolver's other
     * word for it — there is no page for it to be about. A question over a
     * page opens the Ask overlay with that page attached, and a research
     * shape opens the same overlay with the shape stated, because those read
     * named pages and the overlay is where pages are named. Both navigate, so
     * both answer null here.
     *
     * On the Ask overlay everything starts here, and the pages on the row
     * decide the shape of a question: over pages it is a summary of exactly
     * those (the one rule, `taskStartRequest`, checks the count), over none
     * it is an errand. A stated shape is taken as stated.
     *
     * The decision is derived, so it can never disagree with the words or the
     * conditions it was made from: a refusal that said "not ready" after the
     * core had become ready would be a stored answer to a question that has
     * since changed.
     */
    val start: TaskStartDecision?
        get() {
            val reading = reading ?: return null
            val hosts = TaskScope.summarizePages(attachedPages).sources
            val template = when (reading) {
                is AddressBarInterpretation.TaskForTaffy -> reading.template
                is AddressBarInterpretation.AskTaffy -> when {
                    hosts.isNotEmpty() -> TaskTemplate.SUMMARIZE_EVIDENCE
                    conditions.asksInPlace || conditions.onBlankTab -> TaskTemplate.WEB_ERRAND
                    else -> return null
                }
                is AddressBarInterpretation.BrowserCommand,
                is AddressBarInterpretation.GoTo,
                is AddressBarInterpretation.Search,
                -> return null
            }
            if (template != TaskTemplate.WEB_ERRAND && !conditions.asksInPlace) return null
            if (attachedPages.any { it.closed }) {
                return TaskStartDecision.Refused(TaskStartRefusal.PAGE_CLOSED)
            }
            return taskStartRequest(
                goal = reading.input,
                template = template,
                sourceHosts = hosts,
                readiness = conditions.readiness,
                availability = conditions.availability,
                taskAlreadyRunning = conditions.taskAlreadyRunning,
                attachedStores = attachedStores,
            )
        }

    /**
     * Whether choosing [interpretation] is a start from this box rather than a
     * navigation away from it: it is the reading the box shows, and the start
     * rule accepted it.
     */
    fun startsHere(interpretation: AddressBarInterpretation): Boolean =
        interpretation == reading && start is TaskStartDecision.Start

    /**
     * Whether choosing [interpretation] is refused by the start rule, so the
     * box keeps its words and the row under it says why.
     */
    fun refusedHere(interpretation: AddressBarInterpretation): Boolean =
        interpretation == reading && start is TaskStartDecision.Refused
}
