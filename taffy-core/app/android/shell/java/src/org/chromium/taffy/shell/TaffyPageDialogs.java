// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.content.Context;
import android.view.View;

import androidx.activity.ComponentDialog;
import androidx.annotation.Nullable;

import org.chromium.base.Callback;
import org.chromium.components.browser_ui.modaldialog.AppModalPresenter;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * The dialogs a page asks this window for: a site asking for a permission, and a page's own
 * {@code alert}, {@code confirm} and {@code prompt}.
 *
 * <p><b>What was happening.</b> The window had no {@link ModalDialogManager}. Chromium's permission
 * prompt asks the page's window for one, and without it {@code PermissionDialogCoordinator} dismisses
 * the prompt at once, from inside {@code PermissionRequestManager} building it — before the manager
 * holds the prompt it is being told was dismissed. In a build with checks on, that is {@code
 * DCHECK(view_)} and the browser closes; it did, on a phone, the first time a page asked for the
 * person's location, in the person's own tab and in a tab Taffy opened alike. In a build with checks
 * off the request is dismissed without a word, and the tab keeps the dead prompt, so the page's next
 * request is never answered. Settings SCR-205 says "Sites can ask for your location"; neither
 * behaviour lets them.
 *
 * <p><b>What this gives the window, and what it does not.</b> The prompt is Chromium's own, shown
 * the way Chromium shows it outside a tab strip, and the person answers it: TaffyGo answers nothing
 * on their behalf and Taffy never grants a permission (PAR-PERM-001; the page-intelligence protocol
 * forbids bypassing a permission prompt). The activity hands the same manager out as its own, from
 * {@code createModalDialogManager()}, because Chromium's download prompts ask the activity and not
 * the window; see {@code TaffyBrowserActivity.createModalDialogManager}.
 *
 * <p><b>Why a second presenter.</b> Chrome shows a page's dialogs over the tab, through a tab-modal
 * presenter this window does not have. Here they are shown app-modal, as {@code SearchActivity} shows
 * its own. {@link AppModalPresenter} reports Back as {@link
 * DialogDismissalCause#NAVIGATE_BACK_OR_TOUCH_OUTSIDE}, which {@code ModalDialogManager} accepts only
 * for an app-modal dialog and asserts against for a tab-modal one — so the permission prompt, which
 * is tab-modal, would close the browser the moment the person pressed Back on it. {@link
 * TabDialogPresenter} reports the tab-modal cause instead.
 */
final class TaffyPageDialogs {
    private TaffyPageDialogs() {}

    /** One window's manager for its pages' dialogs and the browser's own. */
    static ModalDialogManager create(Context context) {
        ModalDialogManager manager =
                new ModalDialogManager(new AppModalPresenter(context), ModalDialogType.APP);
        manager.registerPresenter(new TabDialogPresenter(context), ModalDialogType.TAB);
        return manager;
    }

    /** Shows a tab-modal dialog app-modal, and reports Back the way a tab-modal dialog does. */
    static final class TabDialogPresenter extends AppModalPresenter {
        TabDialogPresenter(Context context) {
            super(context);
        }

        @Override
        protected void addDialogView(
                PropertyModel model,
                @Nullable Callback<ComponentDialog> onDialogCreatedCallback,
                @Nullable Callback<View> onDialogShownCallback) {
            super.addDialogView(
                    model,
                    dialog -> {
                        dialog.setOnCancelListener(
                                ignored -> dismissCurrentDialog(DialogDismissalCause.NAVIGATE_BACK));
                        if (onDialogCreatedCallback != null) {
                            onDialogCreatedCallback.onResult(dialog);
                        }
                    },
                    onDialogShownCallback);
        }
    }
}
