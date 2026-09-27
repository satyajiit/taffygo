// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import java.util.Locale

/**
 * Country (ISO 3166-1) and UI language (BCP 47) are independent.
 *
 * English is always offered. Hindi is shipped (`values-hi`) so it is offered
 * in every country, not India-only. SYSTEM means follow the device when that
 * language has a catalogue, otherwise English, and is offered everywhere.
 * Changing country must not wipe a still-valid language.
 *
 * This lives in the pure model layer so persistence and every language chooser
 * make the same decision. Region codes are compared as ISO alpha-2 codes;
 * a malformed code still receives the shipped languages.
 */
object LanguageRegionPolicy {

    /** Languages that may be selected for [regionCode], in display order. */
    fun availableLanguages(regionCode: String): List<AppLanguage> =
        AppLanguage.entries.filter { language -> isAvailable(language, regionCode) }

    /** Explicit locale tags Android may advertise for [regionCode]. */
    fun availableLanguageTags(regionCode: String): List<String> =
        availableLanguages(regionCode).mapNotNull { language ->
            language.languageTag.takeIf(String::isNotEmpty)
        }

    /**
     * Whether [language] is a valid explicit choice for [regionCode].
     *
     * [regionCode] is accepted and ignored: the shipped catalogues are not
     * gated by country. The parameter stays so every chooser keeps calling
     * one function.
     */
    @Suppress("UNUSED_PARAMETER")
    fun isAvailable(language: AppLanguage, regionCode: String): Boolean = when (language) {
        AppLanguage.SYSTEM, AppLanguage.ENGLISH, AppLanguage.HINDI -> true
    }

    /**
     * Return a pair-safe language. Every shipped language is valid in every
     * country, so a still-valid choice is left alone.
     */
    fun coerce(language: AppLanguage, regionCode: String): AppLanguage =
        language.takeIf { isAvailable(it, regionCode) } ?: AppLanguage.ENGLISH

    /**
     * The catalogue language [language] actually draws in, given the device's
     * BCP 47 tag.
     *
     * SYSTEM follows the device when that language has a catalogue, otherwise
     * English. An explicit English or Hindi choice is left alone. Country is
     * not consulted: the chip and the first-run default have to agree with
     * [AppLanguage] even when the stored country is not India.
     */
    fun resolvedLanguage(language: AppLanguage, deviceLanguageTag: String): AppLanguage {
        if (language != AppLanguage.SYSTEM) return language
        val base = deviceLanguageTag.substringBefore('-').lowercase(Locale.ROOT)
        return AppLanguage.entries.firstOrNull { entry ->
            entry.languageTag.isNotEmpty() && entry.languageTag == base
        } ?: AppLanguage.ENGLISH
    }

    /** Normalize a platform ISO country code, rejecting malformed values. */
    fun normalizedRegionCode(value: String): String? =
        value.uppercase(Locale.ROOT).takeIf(REGION_CODES::contains)

    /**
     * A localized country name for [code], with the stable ISO code as a safe
     * fallback when the platform has no words for it or the code is not ISO.
     */
    fun regionDisplayName(code: String, locale: Locale): String {
        val normalized = normalizedRegionCode(code) ?: return code
        val country = Locale.Builder().setRegion(normalized).build().getDisplayCountry(locale)
        return country.ifBlank { normalized }
    }

    private val REGION_CODES = Locale.getISOCountries().toSet()
}
