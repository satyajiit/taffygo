// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Robolectric.buildActivity;
import static org.robolectric.Shadows.shadowOf;

import android.app.Activity;
import android.os.Looper;

import androidx.activity.ComponentDialog;
import androidx.annotation.Nullable;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.R;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogManagerObserver;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.List;

/**
 * A page's dialog is shown by this window, and Back on it closes it rather than the browser.
 *
 * <p><b>What this suite is for.</b> The window had no dialog manager, so a page asking for the
 * person's location had its prompt dismissed while the prompt was still being built, and a build
 * with checks on closed. The fix gives the window a manager with a presenter for a tab's dialogs,
 * and the one way that presenter can go wrong on its own is the dismissal it reports: Chromium's
 * app-modal presenter reports Back with a cause the manager asserts against for a tab's dialog.
 * Chromium's permission prompt is a tab's dialog, so without this a person pressing Back on it
 * would have closed the browser a second way.
 *
 * <p><b>What it cannot prove.</b> That a real page's permission prompt appears and its answer
 * reaches the page. That is Chromium's permission stack and the page's renderer, and the evidence
 * for it is a run on a device.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class TaffyPageDialogsTest {
    private final List<Integer> mDismissals = new ArrayList<>();
    private ModalDialogManager mDialogs;
    private @Nullable ComponentDialog mShown;

    @Before
    public void setUp() {
        // The activity's theme is the one the dialogs are drawn in; this binary has no manifest to
        // name one, so it is set the way Chromium's own permission-prompt tests set it.
        Activity activity = buildActivity(Activity.class).setup().get();
        activity.setTheme(R.style.Theme_BrowserUI_DayNight);
        mDialogs = TaffyPageDialogs.create(activity);
        mDialogs.addObserver(
                new ModalDialogManagerObserver() {
                    @Override
                    public void onDialogCreated(
                            PropertyModel model, @Nullable ComponentDialog dialog) {
                        mShown = dialog;
                    }
                });
    }

    @After
    public void tearDown() {
        if (mDialogs != null) mDialogs.destroy();
    }

    @Test
    public void aTabsDialogIsShown() {
        mDialogs.showDialog(dialog(), ModalDialogType.TAB);

        assertTrue(mDialogs.isShowing());
        assertNotNull(mShown);
        assertTrue(mShown.isShowing());
    }

    /** The defect's second half, as one test: Back on a permission prompt. */
    @Test
    public void backOnATabsDialogClosesItAsATabsDialog() {
        mDialogs.showDialog(dialog(), ModalDialogType.TAB);

        assertNotNull(mShown);
        mShown.cancel(); // What Back does to a cancelable dialog.
        shadowOf(Looper.getMainLooper()).idle();

        assertEquals(List.of(DialogDismissalCause.NAVIGATE_BACK), mDismissals);
        assertFalse(mDialogs.isShowing());
    }

    @Test
    public void backOnAnAppDialogIsLeftAsChromiumReportsIt() {
        mDialogs.showDialog(dialog(), ModalDialogType.APP);

        assertNotNull(mShown);
        mShown.cancel(); // What Back does to a cancelable dialog.
        shadowOf(Looper.getMainLooper()).idle();

        assertEquals(List.of(DialogDismissalCause.NAVIGATE_BACK_OR_TOUCH_OUTSIDE), mDismissals);
        assertFalse(mDialogs.isShowing());
    }

    private PropertyModel dialog() {
        return new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                .with(
                        ModalDialogProperties.CONTROLLER,
                        new ModalDialogProperties.Controller() {
                            @Override
                            public void onClick(PropertyModel model, int buttonType) {}

                            @Override
                            public void onDismiss(PropertyModel model, int dismissalCause) {
                                mDismissals.add(dismissalCause);
                            }
                        })
                .with(ModalDialogProperties.TITLE, "A page asks")
                .build();
    }
}
