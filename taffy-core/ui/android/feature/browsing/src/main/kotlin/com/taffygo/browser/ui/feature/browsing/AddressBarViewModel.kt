// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.api.SavedFlowReviewRepository
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartFailure
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.task.TaskStartRequest
import com.taffygo.browser.ui.core.task.toTaskStartFailure
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.UnavailableVoiceInput
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.VoiceInput
import com.taffygo.browser.ui.core.ui.VoiceInputEvent
import com.taffygo.browser.ui.core.ui.VoiceInputSession
import com.taffygo.browser.ui.core.ui.voiceEntryAfterEvent
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/**
 * The box's one source of truth, on screen SCR-103, at the centre of the
 * start page, and in the Ask overlay over a page.
 *
 * The typed text is transient interface state, so it lives in the saved-state
 * handle and comes back after process death. Nothing else here survives,
 * because nothing else here is true once the box is closed.
 *
 * ## The Ask overlay's box
 *
 * The overlay's destination hands its box what it was opened with —
 * [TaffyDestination.QUESTION], and a shape or a list of tabs when the caller
 * chose them — through the same saved-state handle, and the presence of the
 * question key is what tells this class which host it is on. There every
 * reading starts in place, the pages on the question are chips under the
 * box, and once a task has answered the box stays for the next question:
 * a task that accepts a follow-up gets it as the next turn of its own
 * conversation, and one that does not is started afresh (decision 0137).
 *
 * A draft in that handle is a draft the person is still writing, and nothing
 * more. Every intent that leaves the screen empties the state first — the
 * reducer does it, so a host test can prove it — and this class then saves what
 * the reducer decided. That is why the handle cannot hand a finished address to
 * the box a person opens later on another tab.
 *
 * ## A task starts where it was typed
 *
 * A reading the start rule accepts — an errand, or a question on a blank tab —
 * does not leave the box. It is sent to the core from here, the box waits for
 * the task to appear under a name, and then follows it: the words stay, and
 * the panel under them shows what Taffy is doing. There is no preview screen
 * in between, because the rows under the box already say everything a preview
 * said (decision 0087 section 4), and a screen that repeats a sentence the
 * person has just read is a door behind a door.
 */
class AddressBarViewModel(
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
    private val tasks: TaskRepository,
    private val readiness: TaffyReadinessRepository,
    private val voiceInput: VoiceInput = UnavailableVoiceInput,
    reviews: SavedFlowReviewRepository? = null,
    flowPages: SavedFlowPageRequests? = null,
) : ViewModel() {

    /** Whether this is the Ask overlay's box; see the class comment. */
    private val asksInPlace: Boolean = savedState.contains(TaffyDestination.QUESTION)

    // The overlay's rows are the reading and its terms, and nothing else: a
    // history match under "what is this page for" would be a row that took
    // the person somewhere when they had asked to stay.
    private val suggest: (String) -> List<Suggestion> =
        if (asksInPlace) { _ -> emptyList() } else browser::suggestions

    private val internalState = MutableStateFlow(restore())
    private val follower = TaffyTabFollower(browser, tasks)
    private val repeat = SavedFlowRepeatController(browser, reviews, flowPages, viewModelScope) { saved ->
        internalState.update { it.copy(savedFlows = saved) }
    }
    private var voiceSession: VoiceInputSession = VoiceInputSession.CLOSED
    private var voiceGeneration: Long = 0

    /** What the box renders. */
    val state: StateFlow<AddressBarUiState> = internalState.asStateFlow()

    init {
        viewModelScope.launch {
            browser.suggestionRevision.collect { refreshSuggestions() }
        }
        // The facts the start rule reads, kept current for as long as the box
        // stands: a start refused with "not ready yet" turns into one the row
        // offers the moment the core publishes, with nothing retyped.
        viewModelScope.launch {
            readiness.readiness.collect { verdict ->
                updateConditions { it.copy(readiness = verdict) }
            }
        }
        viewModelScope.launch {
            tasks.state.collect { repository ->
                updateConditions {
                    it.copy(
                        availability = repository.availability,
                        taskAlreadyRunning = repository.tasks.any { task -> task.isUnderWay },
                    )
                }
            }
        }
        viewModelScope.launch {
            browser.tabs.collect { tabs ->
                internalState.update { current ->
                    current.copy(
                        conditions = current.conditions.copy(onBlankTab = tabs.selectedIsBlank()),
                        pages = if (asksInPlace) askPagesSnapshot(tabs) else current.pages,
                        attachedPages = refreshAttachedPages(current.attachedPages, tabs),
                    )
                }
                if (asksInPlace) persistAttachedPages()
            }
        }
    }

    /** Act on something the user did. */
    fun onIntent(intent: AddressBarIntent, navigator: TaffyNavigator) {
        if (intent == AddressBarIntent.StartVoiceInput &&
            internalState.value.voiceEntry !is VoiceEntryState.Closed &&
            internalState.value.voiceEntry !is VoiceEntryState.Error
        ) {
            return
        }
        // Decided on the state the reading was chosen in: the reducer below
        // either keeps the words for a start or empties them for a
        // navigation, and which of the two it did is exactly what `before`
        // still says.
        val before = internalState.value
        if (intent is AddressBarIntent.OpenSavedFlow) {
            repeat.open(intent.review, navigator)
            return
        }
        if (intent == AddressBarIntent.DismissSavedFlows) {
            repeat.clear()
            return
        }
        if (intent.withdrawsSavedFlowReview()) repeat.clear()
        if (intent is AddressBarIntent.Choose && before.canFindSavedFlow(intent.interpretation) &&
            openConversation(before) == null
        ) {
            closeVoiceSession()
            if (repeat.find(intent.interpretation.input) {
                val current = internalState.value
                internalState.value = reduceAddressBar(current, intent, browser::resolve, suggest)
                persistInput()
                commit(current, intent.interpretation, navigator)
            }) return
        }
        if (intent is AddressBarIntent.Choose && before.starting) return
        if (intent == AddressBarIntent.TryAgain && before.started == null) return
        when (intent) {
            AddressBarIntent.CancelVoiceInput,
            AddressBarIntent.ConfirmVoiceInput,
            is AddressBarIntent.Choose,
            AddressBarIntent.Dismiss,
            AddressBarIntent.Left,
            -> closeVoiceSession()
            else -> Unit
        }
        val after = reduceAddressBar(
            state = before,
            intent = intent,
            resolve = browser::resolve,
            suggest = suggest,
        )
        internalState.value = after.copy(savedFlows = repeat.state.value)
        persistInput()

        when (intent) {
            is AddressBarIntent.Choose -> commit(before, intent.interpretation, navigator)
            AddressBarIntent.StartVoiceInput -> beginVoiceInput(navigator)
            AddressBarIntent.Dismiss -> navigator.goBack()
            // The same request again, from the ended task's panel. The
            // reducer has put the request back into the box where the
            // overlay had emptied it, and `commit` reads the start rule's
            // answer for the state it left exactly as a tap on the row would:
            // a start, or the set-up screen over the box. The ended task is
            // no longer `started` in that state, so this is a fresh start and
            // never a follow-up.
            AddressBarIntent.TryAgain -> after.reading?.let {
                internalState.value = after.copy(starting = false)
                onIntent(AddressBarIntent.Choose(it), navigator)
            }
            AddressBarIntent.RetryCore -> viewModelScope.launch { tasks.retryCore() }
            // Home is already the second door out of a task (decision 0132
            // section 2), and its own copy says it "brings the start page back
            // whole". Nothing carried that meaning past the box: the reducer
            // emptied the text and the ended task went on holding the bar and
            // the start row for the rest of the run. The id comes off the
            // state before the reducer ran, because the reducer is what clears
            // it.
            AddressBarIntent.LeaveTask -> before.started?.let { tasks.putAway(it.id) }
            // Everything else the box can be asked to do is a change to what
            // is on screen and nothing more: the plus opening and closing, a
            // shape stated or taken off, a store attached or taken off, a
            // keystroke, a transcript. None of them navigates, and none of
            // them starts anything.
            is AddressBarIntent.InputChanged,
            AddressBarIntent.CancelVoiceInput,
            AddressBarIntent.ConfirmVoiceInput,
            AddressBarIntent.OpenMenu,
            AddressBarIntent.DismissMenu,
            is AddressBarIntent.ChooseShape,
            AddressBarIntent.ClearShape,
            is AddressBarIntent.ToggleStore,
            is AddressBarIntent.RemovePage,
            AddressBarIntent.OpenAttachPages,
            AddressBarIntent.DismissAttachPages,
            is AddressBarIntent.ConfirmAttachPages,
            // Leaving is the one of these that changes what is on screen and
            // still goes nowhere: the box emptied itself in the reducer above,
            // and the surface it was drawn on has already gone.
            AddressBarIntent.Left,
            AddressBarIntent.DismissSavedFlows,
            is AddressBarIntent.OpenSavedFlow,
            -> Unit
        }
    }

    /**
     * Speech, from the tap on the microphone to the reading it chooses.
     *
     * A finished transcript is the request: it goes into the box and its
     * reading is chosen at once, with no review step and no second tap
     * (screen SCR-709). The permission prompt, the listening state and a
     * failure keep their overlay, because each of those is something the
     * person has to see; a transcript they can already see in the box.
     */
    private fun beginVoiceInput(navigator: TaffyNavigator) {
        val generation = ++voiceGeneration
        var terminal = false
        val opened = voiceInput.listen { event ->
            if (generation != voiceGeneration || terminal) return@listen
            internalState.value = internalState.value.copy(
                voiceEntry = voiceEntryAfterEvent(internalState.value.voiceEntry, event),
            )
            if (event is VoiceInputEvent.Ready || event is VoiceInputEvent.Failed) {
                terminal = true
                voiceSession = VoiceInputSession.CLOSED
            }
            if (event is VoiceInputEvent.Ready) submitTranscript(navigator)
        }
        if (generation == voiceGeneration && !terminal) {
            voiceSession = opened
        } else {
            opened.close()
        }
    }

    private fun submitTranscript(navigator: TaffyNavigator) {
        onIntent(AddressBarIntent.ConfirmVoiceInput, navigator)
        val reading = internalState.value.reading ?: return
        onIntent(AddressBarIntent.Choose(reading), navigator)
    }

    /** Re-project unchanged text after a background suggestion index replacement. */
    private fun refreshSuggestions() {
        val current = internalState.value
        if (current.input.isBlank()) return
        internalState.value = addressBarShowing(
            input = current.input,
            resolve = browser::resolve,
            suggest = suggest,
        ).keeping(current).copy(
            voiceEntry = current.voiceEntry,
            menuOpen = current.menuOpen,
            startFailure = current.startFailure,
            savedFlows = current.savedFlows,
        )
    }

    private fun updateConditions(change: (StartConditions) -> StartConditions) {
        internalState.update { it.copy(conditions = change(it.conditions)) }
    }

    private fun closeVoiceSession() {
        voiceGeneration++
        voiceSession.close()
        voiceSession = VoiceInputSession.CLOSED
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.AddressBar.screenId))
    }

    /**
     * Act on the chosen reading: start it here, or leave the box for wherever
     * it goes.
     *
     * **Every navigation is [TaffyNavigator.replaceCurrent] and none of them
     * is `goTo`, and that is the difference between a browser whose back button
     * works and one whose does not.** Screen SCR-103 is a box the person opens,
     * uses once, and is finished with; a committed address is precisely the
     * moment it stops being somewhere they could want to return to. Pushing
     * SCR-101 on top of it left the pair behind, so the stack grew by two per
     * navigation and the system back button walked back into an address bar
     * instead of into the page. Replacing means the browsing surface below is
     * *returned to*, with the box dropped on the way — a pop, as
     * `BackStack.replaceCurrent` describes, however deep the browsing chrome
     * had stacked before the box opened.
     *
     * The one `goTo` is set-up: the AI & providers screen opens *over* the box
     * with the words still in it, so that coming back finds the request where
     * it was left and the row now offering to start it.
     */
    private fun commit(
        before: AddressBarUiState,
        interpretation: AddressBarInterpretation,
        navigator: TaffyNavigator,
    ) {
        if (interpretation == before.reading) {
            when (val decision = before.start) {
                is TaskStartDecision.Start -> {
                    val open = openConversation(before)
                    if (open != null) {
                        followUp(open, decision.request, navigator)
                    } else {
                        startHere(decision.request, navigator)
                    }
                    return
                }
                is TaskStartDecision.Refused -> {
                    if (decision.reason == TaskStartRefusal.SETUP_NEEDED) {
                        navigator.goTo(TaffyDestination.AiAndProviders)
                    }
                    return
                }
                null -> Unit
            }
        }

        // Where the reading goes, and what it carries, is `addressBarDestination`
        // — a pure function, so what the person typed reaching the sheet is
        // something a host test can hold rather than something only a phone can
        // show.
        val destination = addressBarDestination(interpretation)

        // A browser command names an existing TaffyGo screen. It is navigation,
        // not a web commit — especially the clear-data command, which opens the
        // reviewed settings flow and never clears anything from the box.
        if (interpretation is AddressBarInterpretation.BrowserCommand) {
            navigator.replaceCurrent(destination)
            return
        }

        // A task in a research shape, and a question about a page, are the Ask
        // overlay's to finish: nothing is committed to the browser on the way.
        if (interpretation is AddressBarInterpretation.TaskForTaffy ||
            interpretation is AddressBarInterpretation.AskTaffy
        ) {
            navigator.replaceCurrent(destination)
            return
        }
        viewModelScope.launch {
            browser.commit(interpretation)
            navigator.replaceCurrent(destination)
        }
    }

    /**
     * The task this box may ask something more, or null when the next send
     * is a fresh start.
     *
     * Only the Ask overlay's box keeps a conversation, and only a task that
     * has finished accepts a question on it; a task that failed, was stopped
     * or is gone from the core's list is started afresh, and one still
     * working is refused by the start rule before this is asked.
     */
    private fun openConversation(before: AddressBarUiState): String? {
        if (!asksInPlace) return null
        val started = before.started ?: return null
        val task = tasks.state.value.tasks.firstOrNull { it.id == started.id } ?: return null
        return task.id.takeIf { task.acceptsFollowUp }
    }

    /**
     * Ask the open conversation the next question (decision 0137).
     *
     * Admitted, the words leave the box and the answer streams under them in
     * the conversation panel; the task stays the box's. A core that no longer
     * holds the transcript — a task restored into a new generation — refuses
     * the command as an invalid request, and that refusal is not an error to
     * show: it is the conversation being gone, and the same words start a
     * fresh task. Every other refusal is said under the box.
     */
    private fun followUp(taskId: String, request: TaskStartRequest, navigator: TaffyNavigator) {
        viewModelScope.launch {
            when (val result = tasks.followUp(taskId, request.goal)) {
                is TaffyResult.Success -> internalState.update { current ->
                    current.asked().copy(starting = false, startFailure = null)
                }
                is TaffyResult.Failure ->
                    if (result.reason == FailureReason.INVALID_REQUEST) {
                        startHere(request, navigator)
                    } else {
                        internalState.update {
                            it.copy(starting = false, startFailure = result.reason.toTaskStartFailure())
                        }
                    }
            }
        }
    }

    /**
     * Send the start and wait for the task to appear under a name.
     *
     * The command being admitted is not the task existing: the task arrives on
     * the next status publication, and it is found by being a task the core
     * did not hold before the start was sent — the whole list is read, because
     * the new task is not the followed one until this code says so. The wait
     * is bounded; a start that is admitted and never appears is reported as
     * the deadline it is, under the box, rather than left spinning.
     */
    private fun startHere(request: TaskStartRequest, navigator: TaffyNavigator) {
        internalState.update { it.copy(starting = true, startFailure = null) }
        val previousTaskIds = tasks.state.value.tasks.map { it.id }.toSet()
        // The tabs as they stand before the start goes out, so a Taffy tab the
        // core opens between admission and the task being named still reads
        // as new to the follower.
        val tabsBefore = browser.tabs.value
        viewModelScope.launch {
            val result = tasks.startTask(
                request.goal,
                request.template,
                request.consent,
                workspaceId = null,
            )
            if (result is TaffyResult.Failure) {
                internalState.update {
                    it.copy(starting = false, startFailure = result.reason.toTaskStartFailure())
                }
                return@launch
            }
            val started = withTimeoutOrNull(START_ACCEPTANCE_TIMEOUT_MILLIS) {
                tasks.state
                    .map { repository -> repository.tasks.firstOrNull { it.id !in previousTaskIds } }
                    .filterNotNull()
                    .first()
            }
            if (started != null) tasks.follow(started.id)
            internalState.update { current ->
                if (started == null) {
                    current.copy(starting = false, startFailure = TaskStartFailure.DEADLINE_EXCEEDED)
                } else current.asked().copy(
                    starting = false,
                    started = StartedTask(id = started.id, goal = request.goal),
                    startFailure = null,
                )
            }
            persistInput()
            // A comparison opens its pages and activity together, keeping
            // Ask beneath it for the same conversation on Back. Other
            // research answers in place; errands follow their first page.
            if (started != null && request.template == TaskTemplate.COMPARE_PRODUCTS) {
                navigator.goTo(TaffyDestination.TaskView)
            } else if (started != null && request.template == TaskTemplate.WEB_ERRAND) {
                follower.follow(started.id, tabsBefore, navigator)
            }
        }
    }

    /**
     * The box once its words have been admitted: on the Ask overlay, empty
     * for the next question, because the words now stand in the conversation
     * above it; elsewhere, unchanged, because the words are the panel's title.
     */
    private fun AddressBarUiState.asked(): AddressBarUiState =
        if (conditions.asksInPlace) {
            addressBarShowing(NO_DRAFT, browser::resolve, suggest).keeping(this)
        } else {
            this
        }

    /**
     * The box as it opens, on whatever draft the handle still holds — which is
     * a draft interrupted by process death, or nothing at all.
     *
     * It opens through the reducer's own projection rather than a second copy
     * of it here. A screen that restores itself by hand is a screen with two
     * answers to "what does this input show", and only one of them is tested.
     */
    private fun restore(): AddressBarUiState {
        val tabs = browser.tabs.value
        return addressBarShowing(
            input = restoredInput(),
            resolve = browser::resolve,
            suggest = suggest,
        ).copy(
            shape = restoredShape(),
            attachedPages = restoredAttachedPages(tabs),
            pages = if (asksInPlace) askPagesSnapshot(tabs) else AskPagesSnapshot(),
            conditions = StartConditions(
                readiness = readiness.readiness.value,
                availability = tasks.state.value.availability,
                taskAlreadyRunning = tasks.state.value.tasks.any { it.isUnderWay },
                onBlankTab = tabs.selectedIsBlank(),
                asksInPlace = asksInPlace,
            ),
        )
    }

    /**
     * The draft, else the question the overlay was opened with.
     *
     * The draft is what this box last saved and wins when there is one; the
     * question is the destination's argument, put in the handle when the
     * overlay opened and read once, on a box that has saved nothing yet. On
     * a private tab the draft is never saved, so the question is all there
     * is — and it is the caller's words, handed over on purpose, not a draft
     * this box let outlive the tab.
     */
    private fun restoredInput(): String {
        val draft = if (selectedTabIsPrivate()) null else savedState.get<String>(INPUT_KEY)
        return draft ?: savedState.get<String>(TaffyDestination.QUESTION).orEmpty()
    }

    /**
     * The shape the box was carrying, or none.
     *
     * Saved beside the draft rather than with it, because it is not the
     * person's words: it is a compiled-in label, and an unknown one — a build
     * that dropped a template — restores as no shape rather than as a crash.
     * It follows the draft's private-tab rule all the same, so that nothing
     * about a private tab's question outlives the tab. The overlay's opening
     * shape is read the way its question is: once, when no draft has been
     * saved over it.
     */
    private fun restoredShape(): TaskTemplate? {
        if (selectedTabIsPrivate()) return null
        val label = savedState.get<String>(SHAPE_KEY)
            ?: savedState.get<String>(TaffyDestination.SHAPE)
            ?: return null
        return TaskTemplate.entries.firstOrNull { it.label == label }
    }

    /**
     * The pages on the question as the overlay opens.
     *
     * What this box saved wins, then what the overlay was opened with — the
     * switcher's picked tabs — and with neither the page the overlay stands
     * over, which is what Ask Taffy from the pill means. An empty saved list
     * is a choice, not an absence: the person took every page off, and the
     * page under the overlay does not come back on its own.
     */
    private fun restoredAttachedPages(tabs: List<Tab>): List<AttachedPage> {
        if (!asksInPlace) return emptyList()
        val saved = savedState.get<String>(ATTACHED_KEY)
            ?: savedState.get<String>(TaffyDestination.ATTACHED_TAB_IDS)
            ?: return defaultAttachedPages(tabs)
        val ids = saved.split(',').filter { it.isNotEmpty() }.map(::TabId)
        return attachedPagesFromIds(tabs, ids)
    }

    /** Private-tab address drafts, including confirmed speech, never enter saved state. */
    private fun persistInput() {
        if (asksInPlace) persistAttachedPages()
        if (selectedTabIsPrivate()) {
            savedState.remove<String>(INPUT_KEY)
            savedState.remove<String>(SHAPE_KEY)
            return
        }
        savedState[INPUT_KEY] = internalState.value.input
        val shape = internalState.value.shape
        if (shape == null) {
            savedState.remove<String>(SHAPE_KEY)
        } else {
            savedState[SHAPE_KEY] = shape.label
        }
    }

    /**
     * The pages on the question, as tab identities and nothing else: no
     * title, no host, so nothing about a page is in the handle that the tab
     * itself does not still hold.
     */
    private fun persistAttachedPages() {
        savedState[ATTACHED_KEY] =
            internalState.value.attachedPages.joinToString(",") { it.tabId.value }
    }

    private fun selectedTabIsPrivate(): Boolean =
        browser.tabs.value.any { tab -> tab.isSelected && tab.isPrivate }

    private companion object {
        const val INPUT_KEY = "address_bar_input"
        const val SHAPE_KEY = "address_bar_shape"
        const val ATTACHED_KEY = "address_bar_attached"

        /** The box with nothing in it, once its words have gone to the core. */
        const val NO_DRAFT = ""

        /**
         * How long an admitted start may take to appear as a task. Generous,
         * because the core answers in milliseconds once it is up and the only
         * thing this bounds is a start the core admitted and then lost.
         */
        const val START_ACCEPTANCE_TIMEOUT_MILLIS = 5_000L
    }

    override fun onCleared() {
        closeVoiceSession()
    }
}

/**
 * Whether the tab under the box has been nowhere, which is what makes a
 * question typed there an errand rather than an ask about a page. No tab at
 * all counts the same way: there is no page either.
 */
private fun List<Tab>.selectedIsBlank(): Boolean =
    firstOrNull { it.isSelected }?.host.isNullOrEmpty()
