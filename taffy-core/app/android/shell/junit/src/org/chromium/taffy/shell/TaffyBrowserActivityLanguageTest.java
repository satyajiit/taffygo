// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.res.Configuration;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * The joint the interface language hangs off, checked the way a rebase breaks it.
 *
 * <p>Below API 33 the platform has no per-app locale, so screen SCR-407's choice reaches the
 * interface by one route only: {@code TaffyBrowserActivity.applyOverrides} puts it into the
 * activity's override configuration, and upstream calls that from {@code attachBaseContext} —
 * before {@code onCreate}, before the theme overlays, and before a single resource has been
 * resolved in the wrong language. {@link org.chromium.taffy.host.TaffyAppLanguage} owns
 * the behaviour and its own suite covers it on both sides of the seam.
 *
 * <p><b>What is left for this file is the attachment, and it is worth a test of its own.</b> An
 * override that stops overriding still compiles. If upstream renames or withdraws the hook,
 * TaffyGo's method becomes an ordinary method nobody calls, no build says anything, and the
 * interface goes quietly back to English on every device below API 33 — a range this project has
 * no phone in, so nothing else here would notice. That is the same technique, and the same reason,
 * as {@link TaffyBrowserActivityWindowTest}.
 *
 * <p><b>What it therefore does not prove:</b> that a device redraws in the chosen language. The
 * evidence for that is a run on a device.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyBrowserActivityLanguageTest {

    @Test
    public void upstreamStillOffersTheConfigurationOverrideHook() throws NoSuchMethodException {
        Method upstream =
                AsyncInitializationActivity.class.getDeclaredMethod(
                        "applyOverrides", Context.class, Configuration.class);

        assertEquals(
                "upstream reads this boolean to decide whether to call"
                        + " applyOverrideConfiguration at all; anything else is a different hook",
                boolean.class,
                upstream.getReturnType());
        assertFalse(
                "a final applyOverrides would mean upstream had withdrawn the seam",
                Modifier.isFinal(upstream.getModifiers()));
    }

    @Test
    public void theActivityOverridesIt() throws NoSuchMethodException {
        Method override =
                TaffyBrowserActivity.class.getDeclaredMethod(
                        "applyOverrides", Context.class, Configuration.class);

        assertEquals(boolean.class, override.getReturnType());
        assertTrue(
                "it has to stay reachable from upstream's own call site, which is in the"
                        + " superclass",
                Modifier.isProtected(override.getModifiers()));

        // Upstream's own override sets the tablet width and calls its super for night mode, so
        // TaffyGo's has to combine with that answer rather than replace it. That it does is a fact
        // about the body, which reflection cannot read; that there is a super to combine with is
        // what this asserts.
        assertTrue(AsyncInitializationActivity.class.isAssignableFrom(TaffyBrowserActivity.class));
    }
}
