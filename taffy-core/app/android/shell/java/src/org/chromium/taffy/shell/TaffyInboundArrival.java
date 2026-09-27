// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

/**
 * Whether an address that arrived on an intent is still owed, decided without an activity.
 *
 * <p><b>Why this is a class and not two lines in the activity.</b> It was two lines in the
 * activity, and they lost inbound links silently. {@link TaffyBrowserActivity} reads an address off
 * an intent in two places — the launch intent, once the tab model exists, and every intent that
 * arrives afterwards through {@code onNewIntentWithNative} — and those two readers need
 * <i>different</i> answers to the same question. Written as one shared reader they got the same
 * answer, and the answer was wrong for one of them on every rebuilt activity.
 *
 * <p><b>The defect this exists to make impossible.</b> Android replays an activity's original
 * intent when it rebuilds it, so a launch intent must be refused when the activity came back from
 * saved state — otherwise one tapped link opens a tab again on every rotation, every return from
 * Recents and every restore after a process kill. That guard was applied to both readers. But
 * {@code getSavedInstanceState()} does not become null again: it answers non-null for the whole
 * life of a rebuilt instance. So from the first configuration change onwards, <i>every</i> link
 * another application sent was read as a replay of that same launch and dropped — the browser came
 * to the front, opened nothing, and said nothing, until the person force-stopped it. Measured on a
 * device: with the activity rebuilt, three inbound {@code VIEW} intents in a row each carried an
 * address, each found a live tab model, and none of them opened a tab.
 *
 * <p>An intent delivered to an activity that is already running is <b>never</b> a replay. Android
 * has nothing to replay it from: the instance it is handed to is the one that was already there.
 * That is the whole of the distinction {@link Delivery} draws, and it is a fact about how the
 * intent arrived rather than about what it carries, which is why it cannot be recovered by
 * inspecting the intent afterwards and has to be passed in by the caller that knows.
 *
 * <p><b>The second rule here is about not overwriting a debt.</b> An intent can arrive before there
 * is anywhere to put it: {@code onNewIntentWithNative} runs as soon as native initialization has
 * finished, and the tab model is built one step further on, when the profile arrives. That window
 * is real and observable — on a cold start the activity's first {@code onNewIntentWithNative} runs
 * roughly a fifth of a second before its tab model exists. An address that arrives in it is parked,
 * and the launch reader that runs next must not write over what is parked, or the link is lost for
 * a second reason with no configuration change involved at all.
 *
 * <p>Everything here is a pure function of two booleans and two strings, so
 * {@code TaffyInboundArrivalTest} drives every combination on a laptop. The address itself is
 * carried through untouched and is never inspected: what may be opened is
 * {@link TaffyInboundIntent}'s decision, and this class only answers when.
 */
public final class TaffyInboundArrival {

    private TaffyInboundArrival() {}

    /**
     * How an intent reached the activity, which is the fact the replay guard turns on.
     *
     * <p>Named for the delivery rather than for the callback, because the callback names are
     * upstream's and a rebase may rename them; what has to stay true is which of the two situations
     * the caller is in.
     */
    public enum Delivery {
        /**
         * The intent the activity was created with, read once the tab model exists.
         *
         * <p>This one may be a replay. Android hands a rebuilt activity the intent that started it,
         * so an address here has usually been acted on already by the instance that came before.
         *
         * <p><b>Upstream draws the same distinction and only half-applies it.</b>
         * {@code IntentHandler.rewriteFromHistoryIntent}, which
         * {@code AsyncInitializationActivity} calls on the activity's behalf, neutralises the
         * Recents case; the saved-state case it neutralises only when
         * {@code ClearIntentWhenRecreated} is enabled. The refusal here is the other half of
         * upstream's own condition, applied unconditionally, so the behaviour does not depend on a
         * field trial.
         */
        LAUNCH,

        /**
         * An intent handed to an activity that was already running.
         *
         * <p>This one never is. It is a person tapping a link in another application right now, and
         * refusing it is refusing the only thing they asked for.
         */
        WHILE_RUNNING,
    }

    /**
     * What the activity still owes after an intent arrived.
     *
     * @param delivery how the intent that carried {@code arriving} reached the activity.
     * @param rebuiltFromSavedState whether this activity instance was rebuilt from saved state,
     *     which for a {@link Delivery#LAUNCH} intent is what makes it a replay. Ignored for
     *     {@link Delivery#WHILE_RUNNING}, which cannot be one.
     * @param owed what was already owed and has not been acted on yet — an address parked because
     *     it arrived before there was a tab model to open it in.
     * @param arriving what the intent that just arrived carries, or null when it carries nothing of
     *     this kind. An intent carries an address or a search, never both, which is
     *     {@link TaffyInboundIntent}'s guarantee rather than this one's.
     * @return the value the activity should now hold as owed. Null means nothing is owed, which is
     *     the ordinary case: most intents are a person opening the browser rather than sending it
     *     somewhere.
     */
    public static @Nullable String owedAfter(
            Delivery delivery,
            boolean rebuiltFromSavedState,
            @Nullable String owed,
            @Nullable String arriving) {
        // A debt already taken on outranks anything an intent says, in both deliveries. It is the
        // same address in the ordinary case — Android sets the arriving intent as the activity's
        // own, so the launch reader that follows sees it a second time — and keeping the parked
        // copy is what makes the two readings one tab rather than two, or none.
        if (owed != null) return owed;
        if (delivery == Delivery.WHILE_RUNNING) return arriving;
        return rebuiltFromSavedState ? null : arriving;
    }
}
