// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.app.Activity
import android.app.LocaleConfig
import android.app.LocaleManager
import android.content.Context
import android.content.ContextWrapper
import android.content.res.Configuration
import android.content.res.Resources
import android.os.Build
import android.os.LocaleList
import androidx.annotation.RequiresApi
import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.LanguageRegionPolicy
import org.chromium.base.LocaleUtils

/**
 * Where the chosen interface language stops being a stored value and becomes
 * the language the interface is drawn in.
 *
 * WHAT WAS WRONG BEFORE THIS FILE. Screen SCR-407 and the first-run language
 * chip both wrote [AppLanguage] to the preference store, the store kept it
 * across a force-stop, and the control read it back and marked it selected —
 * and not one word of the interface changed, in either build, on either route.
 * The whole `values-hi` translation set was therefore unreachable by any user
 * while `check_strings.py` went on requiring every change to maintain it.
 *
 * WHY THE MECHANISM SPLITS AT API 33, AND WHY THAT IS NOT A CHOICE MADE HERE.
 * The seam is the platform's, and Chromium already sits on the same side of it:
 * `AppLocaleUtils.shouldUseSystemManagedLocale()` is `SDK_INT >= TIRAMISU`, and
 * on that branch `GlobalAppLocaleController.init` sets `mIsOverridden = false`
 * unconditionally — so from API 33 up, Chromium applies **no** locale override
 * of its own and defers entirely to the framework's per-app locale. AppCompat
 * splits at the same version for the same reason.
 *
 *  - **API 33 and up — the framework's per-app locale.** `LocaleManager` is the
 *    one store, the system applies it to the application context and to
 *    `Locale.getDefault()` before any TaffyGo code runs, persists it itself, and
 *    recreates the activity when it changes. Chromium reads that same store
 *    through `AppLocaleUtils.getAppLanguagePref()`, so the browser process and
 *    this interface cannot end up holding different answers.
 *  - **API 29 to 32 — the activity's own override configuration.** The platform
 *    has no per-app locale at all there. [overrideConfiguration] is called from
 *    `TaffyBrowserActivity.applyOverrides`, which upstream invokes from
 *    `attachBaseContext` before a single resource is resolved, and it performs
 *    exactly the pair of calls `GlobalAppLocaleController.maybeOverrideContextConfig`
 *    performs on that API range: prepend the tag to the base context's locale
 *    list, then move the process default locales to match, so the native side
 *    reads the same language the resources do.
 *
 * WHAT APPLYING A LANGUAGE COSTS, MEASURED RATHER THAN ASSUMED. Above API 33
 * the framework restarts the whole application to apply a per-app locale, and
 * nothing here asks for that or can decline it. On the API 36 device this was
 * measured on (2026-08-21) the process is replaced, the interface comes back in
 * the chosen language — browser process included, because the restart reloads
 * the locale pak — and the person is left looking at the launcher rather than at
 * the browser they were using. Below API 33 the mechanism is [apply]'s own
 * `recreate()` and the browser stays where it is. **The two API ranges therefore
 * behave differently at exactly this moment**, which is why what the screens say
 * about it is registered as OD-098 rather than decided here: Chrome's answer is
 * an explicit restart the person is offered rather than one performed under
 * them, and offering one is new copy on two screens in both locales.
 *
 * That is also why every comparison below is written to avoid setting a locale
 * that changes nothing: an unnecessary set is not a wasted call, it is a browser
 * that closes itself for no reason.
 *
 * WHY THE PREFERENCE STORE STAYS THE ONE TRUTH. On API 33 and up the platform
 * keeps a copy of the choice, which means it can be changed from Android's own
 * app settings and disagree with TaffyGo's store. [apply] runs on every launch
 * and drives the platform to whatever the store says, so the disagreement is
 * resolved in one direction and always the same one.
 */
object TaffyAppLanguage {

    /**
     * The language this process's TaffyGo activities were configured for, or
     * null on a host that does not call [overrideConfiguration] at all.
     *
     * Only read below API 33, where the recreation is TaffyGo's to ask for.
     * Above it the platform owns both the store and the recreation and this
     * value is never consulted.
     */
    @Volatile
    private var configured: AppLanguage? = null

    /** True where the framework owns the per-app locale — API 33 and up. */
    @JvmStatic
    fun platformOwnsPerAppLocale(): Boolean =
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

    /** What [overrideConfiguration] last recorded; see [configured]. */
    @JvmStatic
    @VisibleForTesting
    fun configuredLanguage(): AppLanguage? = configured

    /**
     * Puts the stored language into an activity's override configuration.
     *
     * The two-argument form is what `TaffyBrowserActivity.applyOverrides`
     * calls; the three-argument form below is the same thing with the answer
     * handed in, so a test can state the language rather than arrange a store.
     */
    @JvmStatic
    fun overrideConfiguration(base: Context, config: Configuration): Boolean =
        configured?.let { overrideConfiguration(base, config, it) } ?: false

    /**
     * Puts [language] into [config], and returns whether anything was put there.
     *
     * Returns false — leaving [config] untouched — on API 33 and up, where the
     * platform has already applied the choice to the whole application and a
     * second opinion here could only contradict it, and for
     * [AppLanguage.SYSTEM], which *is* the base context's own answer. The
     * system case still moves the process default locales back, because the
     * activity being replaced may have moved them away from the device's.
     */
    @JvmStatic
    fun overrideConfiguration(
        base: Context,
        config: Configuration,
        language: AppLanguage,
    ): Boolean {
        configured = language
        if (platformOwnsPerAppLocale()) return false
        val tag = language.languageTag
        if (tag.isEmpty()) {
            val device = base.resources.configuration
            // `LocaleList.setDefault` refuses an empty list, and a base context
            // with no locales at all is not a thing this can put right anyway.
            if (!device.locales.isEmpty) {
                LocaleUtils.setDefaultLocalesFromConfiguration(device)
            }
            return false
        }
        LocaleUtils.updateConfig(base, config, tag)
        LocaleUtils.setDefaultLocalesFromConfiguration(config)
        return true
    }

    /**
     * Makes [language] the language the interface is drawn in, now.
     *
     * Called from composition whenever the stored choice arrives or changes,
     * and safe to call repeatedly: both branches compare before they act, so a
     * call that asks for the language already in force does nothing at all.
     * The country policy is applied again at this final boundary so a stale
     * pair is coerced the same way persistence coerces it. Country no longer
     * hides Hindi or SYSTEM; either branch acting unconditionally would
     * recreate the activity on every recomposition.
     *
     * Takes a [Context] rather than an `Activity` because the caller is a
     * composable, and `LocalContext` is whatever the composition was hosted in.
     * Only the pre-33 branch needs the activity itself, and it is the one that
     * unwraps for it, so a host that has no activity below it degrades to doing
     * nothing rather than to a cast that throws.
     */
    @JvmStatic
    fun apply(context: Context, language: AppLanguage, regionCode: String) {
        applyAvailableLanguages(context, regionCode)
        val effectiveLanguage = LanguageRegionPolicy.coerce(language, regionCode)
        if (platformOwnsPerAppLocale()) {
            applyThroughThePlatform(context, effectiveLanguage)
            return
        }
        val previousLanguage = configured
        configured = effectiveLanguage
        val needsRecreation = if (previousLanguage == null) {
            effectiveLanguage != AppLanguage.SYSTEM
        } else {
            needsRecreate(previousLanguage, effectiveLanguage)
        }
        if (!needsRecreation) return
        val activity = activityOf(context) ?: return
        if (!activity.isFinishing) activity.recreate()
    }

    /** Keep Android 14+'s settings list level with the shipped catalogues. */
    private fun applyAvailableLanguages(context: Context, regionCode: String) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return
        val manager = context.getSystemService(LocaleManager::class.java) ?: return
        val supported = LocaleList.forLanguageTags(
            LanguageRegionPolicy.availableLanguageTags(regionCode).joinToString(","),
        )
        if (manager.overrideLocaleConfig?.supportedLocales != supported) {
            manager.overrideLocaleConfig = LocaleConfig(supported)
        }
    }

    /** The activity a composition is hosted in, or null if it is not in one. */
    private tailrec fun activityOf(context: Context): Activity? = when (context) {
        is Activity -> context
        is ContextWrapper -> activityOf(context.baseContext)
        else -> null
    }

    /**
     * Whether an activity configured for [configuredLanguage] has to be built
     * again to show [language].
     *
     * A null [configuredLanguage] answers false, and that is the loop breaker
     * rather than a convenience: it means no TaffyGo override was applied to
     * this activity's configuration, so recreating it would produce another
     * activity in the same language and ask the same question again.
     */
    @JvmStatic
    fun needsRecreate(configuredLanguage: AppLanguage?, language: AppLanguage): Boolean =
        configuredLanguage != null && configuredLanguage != language

    /**
     * The locales to hand the framework, or null when the application already
     * runs in the chosen language.
     *
     * **Setting these is never free**, which is what shapes every rule below:
     * the framework restarts the application to apply them, so an answer of
     * "set it again" that changes nothing visible costs the person a restart
     * for nothing. Measured on 2026-08-21 on an API 36 phone: it emptied the
     * task and left the launcher on screen.
     *
     * So the comparison is against the language the application **resolves**,
     * not against the list the framework was last handed. [current] is that
     * list, and an empty one does not mean "no language" — it means "whatever
     * the device is set to", which is [device]. On an English phone, TaffyGo's
     * compiled-in default of English is therefore already in force before
     * anything is set, and the first launch after an install is not spent
     * restarting to reach the state it was already in. The pin is still taken
     * the moment it means something: a Hindi phone, or a person choosing a
     * language that is not the device's.
     *
     * That leaves one window, and it is worth naming rather than hiding. An
     * English phone running unpinned TaffyGo, then switched to Hindi in Android
     * settings, resolves Hindi — and the next composition sees a device
     * language the choice disagrees with, takes the pin, and puts it back. One
     * launch, not a permanent disagreement.
     *
     * The base language is compared rather than the whole tag, because the
     * framework is entitled to normalise what it is given — a request for `hi`
     * may read back as `hi-IN`, and a device says `en-IN` where the choice says
     * `en`. A strict comparison would answer "set it" every single time, which
     * is a restart loop rather than a redundant call. The three values
     * [AppLanguage] carries share no base language, so nothing is lost.
     */
    @JvmStatic
    fun localesToSet(
        current: LocaleList,
        device: LocaleList,
        language: AppLanguage,
    ): LocaleList? {
        val tag = language.languageTag
        if (tag.isEmpty()) {
            // Following the device is the absence of a pin, so the only thing
            // that satisfies it is an empty list.
            return if (current.isEmpty) null else LocaleList.getEmptyLocaleList()
        }
        val effective = if (current.isEmpty) device else current
        val holds = !effective.isEmpty &&
            LocaleUtils.isBaseLanguageEqual(effective.get(0).toLanguageTag(), tag)
        return if (holds) null else localesFor(language)
    }

    /** The framework form of the preference: a locale list, empty for the system choice. */
    private fun localesFor(language: AppLanguage): LocaleList =
        if (language.languageTag.isEmpty()) {
            LocaleList.getEmptyLocaleList()
        } else {
            LocaleList.forLanguageTags(language.languageTag)
        }

    /**
     * The device's own languages, unaffected by any per-app override.
     *
     * `Resources.getSystem()` is the framework's global resource object; its
     * configuration is the device's and never the application's, which is
     * exactly the reading [localesToSet] needs to know what an empty per-app
     * list resolves to. Asking [Context] instead would ask the overridden
     * value and answer its own question.
     */
    private fun deviceLocales(): LocaleList =
        Resources.getSystem().configuration.locales

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    private fun applyThroughThePlatform(context: Context, language: AppLanguage) {
        val manager = context.getSystemService(LocaleManager::class.java) ?: return
        val locales =
            localesToSet(manager.applicationLocales, deviceLocales(), language) ?: return
        manager.applicationLocales = locales
    }
}
