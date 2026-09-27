// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import org.chromium.base.Callback

/**
 * Words that arrived from the system's search widget, answered the way TaffyGo answers its own
 * address bar.
 *
 * ## Why this exists at all
 *
 * Until patch `chromium/patches/0025-taffygo-owns-inbound-intents.md`, an
 * `android.intent.action.WEB_SEARCH` resolved to `SearchActivity` — Chrome's search surface, with
 * Chrome's `url_bar`, `toolbar` and `location_bar_status` on screen inside a product that is not
 * Chrome. The patch moves that filter onto the alias TaffyGo owns, which closes the door but leaves
 * a question the manifest cannot answer: what does TaffyGo *do* with a handful of words?
 *
 * ## The answer is not new, which is the point
 *
 * TaffyGo already decides this, several times a day, for everything a person types into the address
 * bar. `BrowserRepository.resolve` runs `AddressBarResolver`, which reads the words as an address,
 * a search, a question for Taffy, or a task. Committing a `Search` opens the engine chosen in
 * General (default Google, decision 0019). The repository asks `SearchEngineRepository` for that
 * address rather than naming a host itself. If that choice cannot produce an address, it records
 * `BrowserNotice.NO_SEARCH_ENGINE` and screen SCR-101 puts it into TaffyGo's own words. Which
 * engine may ground a model remains OD-019. The repository is a process singleton and the notice is
 * a `StateFlow`, so a notice recorded before the browsing screen exists is waiting there when it
 * does — which is exactly the cold-start case an inbound search produces.
 *
 * So the whole of this file is: ask the resolver, and route its answer to the one thing the
 * resolver cannot do from here.
 *
 * ## The one place it deliberately differs from the address bar
 *
 * `BrowserRepository.commit(GoTo)` navigates the **current** tab, because that is what typing an
 * address into the bar above the page you are reading means. An intent from another application
 * means no such thing, and the rule for the whole inbound path is that nothing another application
 * sends may destroy the page a person was already reading. So a `GoTo` is handed back to the caller
 * — which opens a new tab, exactly as an inbound link does — and only the other three
 * interpretations are committed.
 *
 * ## Why Kotlin, in a directory that is otherwise Java
 *
 * `commit` is a `suspend` function on a Kotlin interface and the interpretations are a sealed
 * hierarchy. Both are natural to read here and awkward from Java, and `ChromiumBrowserMediator` in
 * this same directory is Kotlin for the same reason. The bridge is `@JvmStatic` so
 * [TaffyBrowserActivity] calls it as an ordinary static method.
 */
object TaffyInboundQuery {

    /**
     * Hands words to the resolver and acts on what it says.
     *
     * @param browser the window-owned browser projection.
     * @param scope the window lifetime; pending work is cancelled when the activity closes.
     * @param query the words, already trimmed and known non-empty by [TaffyInboundIntent].
     * @param openInNewTab called, on the calling thread, when the words name a place to go. The
     *   host is raw text; the caller's tab creator puts it through the same `UrlFormatter.fixupUrl`
     *   every other new tab's address goes through.
     */
    @JvmStatic
    fun submit(
        browser: BrowserRepository,
        scope: CoroutineScope,
        query: String,
        openInNewTab: Callback<String>,
    ) {
        val interpretation = browser.resolve(query)
        if (interpretation is AddressBarInterpretation.GoTo) {
            openInNewTab.onResult(interpretation.host)
            return
        }
        // Everything else is the repository's to answer, including the refusal. Launched on the
        // window scope because `commit` is suspending and must not outlive its browser mediator.
        scope.launch { browser.commit(interpretation) }
    }
}
