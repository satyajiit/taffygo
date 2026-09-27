// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.StringRes;

import org.chromium.taffy.shell.R;

/**
 * Screen SCR-802 — what a download says while it runs and when it stops.
 *
 * <p>This is the one M1 surface with no UI layer screen behind it, and the reason is structural
 * rather than an omission: a notification needs a channel, a manifest and a posting context, none of
 * which the UI layer has. It could not have been drawn there honestly, so it is written here, where
 * the platform component actually lives.
 *
 * <p><b>What this class is.</b> The content of a download notification as a pure function of its
 * state — the title, and which actions the state offers. It posts nothing and holds no Android
 * object, so every rule below is a plain unit test rather than a device observation. The posting
 * half is {@link TaffyDownloadNotifier}; separating them keeps platform objects out of this closed
 * state projection.
 *
 * <p><b>Four states, and why not seven.</b> The assistant has seven task states and this has four.
 * A download is not a task: it has no sources, no partial result, and nothing to say "partly done"
 * about. Reusing the task vocabulary would make two different things look like one, which the
 * catalog's copy rules forbid more or less in those words.
 */
public final class TaffyDownloadNotification {

    /** What a download can be doing. Four, and the set is closed. */
    public enum State {
        /** Bytes are arriving. */
        RUNNING,
        /** Stopped by the person, and resumable. */
        PAUSED,
        /** The file is on the device. */
        COMPLETE,
        /** It stopped without the file. */
        FAILED,
    }

    /** An action a notification offers. Each maps to one button. */
    public enum Action {
        /** Pause a resumable active download. */
        PAUSE,
        /** Resume a paused or interrupted resumable download. */
        RESUME,
        /** Cancel an active download. */
        CANCEL,
    }

    private TaffyDownloadNotification() {}

    /** The channel every download notification posts to. */
    public static final String CHANNEL_ID = "taffygo.downloads";

    /**
     * The title for a state.
     *
     * <p>Every one names the file, because a person with three downloads running sees three
     * notifications and a title that did not name the file would make them identical.
     */
    @StringRes
    public static int titleFor(State state) {
        switch (state) {
            case RUNNING:
                return R.string.taffy_download_running;
            case PAUSED:
                return R.string.taffy_download_paused;
            case COMPLETE:
                return R.string.taffy_download_complete;
            case FAILED:
                return R.string.taffy_download_failed;
        }
        // The enum is closed and every member is handled above. This is
        // unreachable, and throwing rather than returning a placeholder means a
        // future member cannot ship showing the wrong words.
        throw new IllegalArgumentException("unhandled download state: " + state);
    }

    /**
     * The actions a state offers, in the order they are shown.
     *
     * <p>Only live provider capabilities become controls. Completion has no action here: open,
     * share and remove remain on SCR-203, where the live item is revalidated and Android can show
     * the appropriate chooser. "Try again" is not synthesized; Chromium exposes only resume.
     */
    public static Action[] actionsFor(
            State state, boolean mayPause, boolean mayResume, boolean mayCancel) {
        switch (state) {
            case RUNNING:
                if (mayPause && mayCancel) {
                    return new Action[] {Action.PAUSE, Action.CANCEL};
                }
                if (mayPause) {
                    return new Action[] {Action.PAUSE};
                }
                if (mayCancel) {
                    return new Action[] {Action.CANCEL};
                }
                return new Action[] {};
            case PAUSED:
                if (mayResume && mayCancel) {
                    return new Action[] {Action.RESUME, Action.CANCEL};
                }
                if (mayResume) {
                    return new Action[] {Action.RESUME};
                }
                if (mayCancel) {
                    return new Action[] {Action.CANCEL};
                }
                return new Action[] {};
            case FAILED:
                return mayResume ? new Action[] {Action.RESUME} : new Action[] {};
            case COMPLETE:
                return new Action[] {};
        }
        throw new IllegalArgumentException("unhandled download state: " + state);
    }

    /** The label for an action. */
    @StringRes
    public static int labelFor(Action action) {
        switch (action) {
            case PAUSE:
                return R.string.taffy_download_pause;
            case RESUME:
                return R.string.taffy_download_resume;
            case CANCEL:
                return R.string.taffy_download_cancel;
        }
        throw new IllegalArgumentException("unhandled download action: " + action);
    }

    /**
     * Whether a state's notification shows a progress bar.
     *
     * <p>Only a running download does. A paused one keeps its notification but stops the bar, so
     * that nothing on screen appears to be moving when nothing is — the same rule the handoff sets
     * for every other loop in the product.
     */
    public static boolean showsProgress(State state) {
        return state == State.RUNNING;
    }

    /**
     * Whether a state's notification can be dismissed by swiping.
     *
     * <p>A finished or failed download can: the person has seen it and there is nothing left to
     * watch. A running or paused one cannot, because it is the only handle on work still in flight
     * and losing it would leave a download with no surface at all.
     */
    public static boolean isDismissable(State state) {
        return state == State.COMPLETE || state == State.FAILED;
    }
}
