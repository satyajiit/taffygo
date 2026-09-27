// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.ChromeBaseAppCompatActivity;
import org.chromium.chrome.browser.init.ActivityProfileProvider;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.DestroyObserver;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.IntentRequestTracker;
import org.chromium.ui.insets.InsetObserver;
import org.chromium.ui.modaldialog.ModalDialogManager;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * The two seams a page needs before it can exist, checked the way a rebase breaks them.
 *
 * <p><b>Why this is reflection and not behaviour.</b> Both methods under test are called by
 * {@code AsyncInitializationActivity.onCreate}, and running that on a JVM means running Chrome's
 * whole pre-native startup: the first-run sequencer, the split-compat class-loader check, the theme
 * overlay pass, the inset observer over a real decor view, and
 * {@code ChromeBrowserInitializer.handlePreNativeStartupAndLoadLibraries}. Calling
 * {@code createWindowAndroid()} <i>without</i> that is not possible either — it reads
 * {@code getInsetObserver()}, which upstream asserts on before {@code onCreate} has built it. A
 * Robolectric test that stood all of it up would be testing upstream's startup, and it would fail
 * for a dozen reasons that have nothing to do with this class.
 *
 * <p>So this checks the thing a laptop genuinely can: that the joints TaffyGo hangs off are still
 * the shape TaffyGo assumes. That is not a formality. Every assertion below corresponds to a way an
 * upstream milestone can break this activity <i>silently</i> — an override that stops overriding
 * still compiles, and the default it fails back to (a null window, an abstract method reimplemented
 * elsewhere) produces a browser that starts and cannot draw a page, with no stack trace naming
 * either file.
 *
 * <p><b>What this therefore does not prove:</b> that the window is constructed correctly at
 * runtime, that the profile provider is ever fulfilled, or that a page appears. Those are on-device
 * facts and the evidence for them is a run on a device.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyBrowserActivityWindowTest {

    @Test
    public void theActivityMakesItsOwnWindow() throws NoSuchMethodException {
        Method upstream =
                AsyncInitializationActivity.class.getDeclaredMethod("createWindowAndroid");
        Method override = TaffyBrowserActivity.class.getDeclaredMethod("createWindowAndroid");

        // Upstream's default is `return null`, and a null window is not an error anywhere — it is
        // simply an activity that can never host web content. An override that stopped matching
        // this signature would compile and would restore that default.
        assertEquals(
                "createWindowAndroid must return the type upstream declares, or it is a new"
                        + " method rather than an override",
                upstream.getReturnType(),
                override.getReturnType());
        assertEquals(ActivityWindowAndroid.class, override.getReturnType());
        assertEquals(0, override.getParameterCount());
    }

    @Test
    public void theActivityHasTheDialogManagerDownloadsAskFor() throws NoSuchMethodException {
        Method upstream =
                ChromeBaseAppCompatActivity.class.getDeclaredMethod("createModalDialogManager");
        Method override =
                TaffyBrowserActivity.class.getDeclaredMethod("createModalDialogManager");

        // Chromium's download prompts ask the activity for its dialog manager, and upstream's
        // default is `return null`. The prompts for a harmful file, a plain-HTTP download and a
        // repeated one call showDialog on that null, which closed the browser on 2026-09-27. An
        // override that stopped matching this signature would compile and restore the default.
        assertEquals(
                "createModalDialogManager must return the type upstream declares, or it is a new"
                        + " method rather than an override",
                upstream.getReturnType(),
                override.getReturnType());
        assertEquals(ModalDialogManager.class, override.getReturnType());
        assertEquals(0, override.getParameterCount());
    }

    @Test
    public void theWindowConstructorTheActivityCallsStillExists() throws NoSuchMethodException {
        // The five-argument overload, which fills in ActivityAndroidPermissionDelegate,
        // ActivityKeyboardVisibilityDelegate and activityTopResumedSupported=false on the caller's
        // behalf. If upstream drops it, the activity has to say those three things itself, and
        // this is where that shows up rather than in a compile error nobody expected.
        assertNotNull(
                ActivityWindowAndroid.class.getConstructor(
                        Context.class,
                        boolean.class,
                        IntentRequestTracker.class,
                        InsetObserver.class,
                        boolean.class));
    }

    @Test
    public void theActivityAnswersUpstreamsAbstractProfileQuestion() throws NoSuchMethodException {
        Method upstream =
                AsyncInitializationActivity.class.getDeclaredMethod("createProfileProvider");
        Method override = TaffyBrowserActivity.class.getDeclaredMethod("createProfileProvider");

        assertTrue(
                "createProfileProvider is upstream's contract with its subclasses; if it stops"
                        + " being abstract, this activity is no longer required to answer it",
                Modifier.isAbstract(upstream.getModifiers()));
        assertEquals(OneshotSupplier.class, override.getReturnType());
    }

    @Test
    public void theProfileProviderIsTheOneUpstreamFulfils() throws NoSuchMethodException {
        // Two facts, and the activity depends on both. It is a OneshotSupplier<ProfileProvider>,
        // so it can be returned from createProfileProvider at all; and it is a DestroyObserver,
        // which is what takes it back off ProfileManager's observer list when the activity goes
        // away. Without the second, a rotated activity leaks its provider into a process-wide
        // list — the kind of defect that is invisible until it is a crash on the tenth rotation.
        assertTrue(OneshotSupplier.class.isAssignableFrom(ActivityProfileProvider.class));
        assertTrue(DestroyObserver.class.isAssignableFrom(ActivityProfileProvider.class));

        // The one-argument construction the activity performs, and the accessor it feeds it from.
        assertNotNull(
                ActivityProfileProvider.class.getConstructor(ActivityLifecycleDispatcher.class));
        assertEquals(
                ActivityLifecycleDispatcher.class,
                AsyncInitializationActivity.class
                        .getMethod("getLifecycleDispatcher")
                        .getReturnType());
    }
}
