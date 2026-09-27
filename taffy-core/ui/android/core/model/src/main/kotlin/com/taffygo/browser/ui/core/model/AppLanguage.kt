// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The language choice of screen SCR-407. The default follows the system
 * locale; English and Hindi are the locales the UI layer ships a translation
 * for (parity row PAR-L10N-001), and both are offered in every country.
 * SYSTEM follows the device when that language has a catalogue, otherwise
 * English. The app language is the interface's language only: it never
 * translates a web page, and the assistant answers in it unless the user
 * asks otherwise (handoff screen 12b).
 */
enum class AppLanguage(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /**
     * The BCP 47 tag the platform applies, empty for the system choice. This
     * module is pure JVM, so the tag is data here and becomes a
     * `LocaleListCompat` in the one module that may know Android: `:app`.
     */
    val languageTag: String,
) {
    /** Follow the device locale. The default. */
    SYSTEM("system", ""),

    /** Always English. */
    ENGLISH("english", "en"),

    /** Always Hindi. */
    HINDI("hindi", "hi"),
}
