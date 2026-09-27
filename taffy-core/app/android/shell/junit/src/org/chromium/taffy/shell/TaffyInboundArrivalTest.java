// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.taffy.shell.TaffyInboundArrival.Delivery;

/**
 * Every combination of how an intent arrived and what the activity already owed.
 *
 * <p><b>Why this suite exists in the shape it does.</b> The defect it was written for was not a
 * wrong answer to a hard question; it was one answer given to two questions. A link tapped in
 * another application was refused because the activity had been rebuilt at some point earlier in
 * its life — the two facts have nothing to do with each other, and the browser showed nothing at
 * all when it happened. The combinations below are small enough to enumerate, so they are
 * enumerated: a table with a hole in it is how the first version passed its own tests.
 *
 * <p>The addresses are arbitrary strings and not URLs on purpose. {@link TaffyInboundArrival}
 * decides <i>when</i> a payload may be acted on and never what it is — that is
 * {@link TaffyInboundIntent}, which has a suite of its own — so a test that used real addresses
 * would suggest a relationship that does not exist.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyInboundArrivalTest {

    private static final String ARRIVING = "arriving";
    private static final String OWED = "owed";

    // -----------------------------------------------------------------------
    // An intent handed to a running activity is never a replay.
    // -----------------------------------------------------------------------

    /**
     * The defect, as one assertion.
     *
     * <p>Measured before the fix, on a Xiaomi 2511FPC34I: with the activity rebuilt by a
     * configuration change, three inbound {@code VIEW} intents in a row each carried an address and
     * each found a live tab model, and none of them opened a tab. This is that state — arriving
     * while running, rebuilt from saved state, nothing owed — and the address has to survive it.
     */
    @Test
    public void aLinkArrivingWhileRunningSurvivesARebuiltActivity() {
        assertEquals(
                ARRIVING,
                TaffyInboundArrival.owedAfter(
                        Delivery.WHILE_RUNNING,
                        /* rebuiltFromSavedState= */ true,
                        /* owed= */ null,
                        ARRIVING));
    }

    @Test
    public void aLinkArrivingWhileRunningIsTakenOnWhenNothingWasRebuilt() {
        assertEquals(
                ARRIVING,
                TaffyInboundArrival.owedAfter(
                        Delivery.WHILE_RUNNING,
                        /* rebuiltFromSavedState= */ false,
                        /* owed= */ null,
                        ARRIVING));
    }

    /** An ordinary launcher tap while the browser is open owes nothing and clears nothing. */
    @Test
    public void anIntentCarryingNothingWhileRunningOwesNothing() {
        assertNull(
                TaffyInboundArrival.owedAfter(
                        Delivery.WHILE_RUNNING,
                        /* rebuiltFromSavedState= */ false,
                        /* owed= */ null,
                        /* arriving= */ null));
    }

    // -----------------------------------------------------------------------
    // A launch intent may be a replay, and on a rebuilt activity it is one.
    // -----------------------------------------------------------------------

    @Test
    public void aLaunchIntentIsTakenOnWhenTheActivityIsNew() {
        assertEquals(
                ARRIVING,
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH,
                        /* rebuiltFromSavedState= */ false,
                        /* owed= */ null,
                        ARRIVING));
    }

    /**
     * The rule the whole guard exists for, and the one that must not be lost while fixing the
     * defect above: Android hands a rebuilt activity the intent that started it, so a launch
     * address on a rebuilt instance has already been opened by the instance before.
     *
     * <p>This is not hypothetical either. On the same device, a configuration change after one
     * inbound link produced a rebuilt activity whose {@code getIntent()} still carried that link's
     * address; without this refusal the tab would have been opened again on every rebuild.
     */
    @Test
    public void aLaunchIntentOnARebuiltActivityIsAReplayAndIsRefused() {
        assertNull(
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH,
                        /* rebuiltFromSavedState= */ true,
                        /* owed= */ null,
                        ARRIVING));
    }

    @Test
    public void aLaunchIntentCarryingNothingOwesNothing() {
        assertNull(
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH,
                        /* rebuiltFromSavedState= */ false,
                        /* owed= */ null,
                        /* arriving= */ null));
    }

    // -----------------------------------------------------------------------
    // A debt already taken on is never written over.
    // -----------------------------------------------------------------------

    /**
     * The second way a link was lost, with no configuration change in it.
     *
     * <p>An intent can arrive before there is a tab model to open it in — measured on a cold start,
     * where the activity's first {@code onNewIntentWithNative} ran about a fifth of a second before
     * its tab model existed. The address is parked, and the launch reader that runs next used to
     * overwrite the field unconditionally. On a rebuilt activity that overwrite was with null.
     */
    @Test
    public void aParkedAddressSurvivesTheLaunchReaderOnARebuiltActivity() {
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH, /* rebuiltFromSavedState= */ true, OWED, null));
    }

    /**
     * The ordinary shape of the same window: Android sets an arriving intent as the activity's own,
     * so the launch reader sees the same address a second time. One debt, one tab.
     */
    @Test
    public void seeingTheSameAddressTwiceKeepsOneDebt() {
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH, /* rebuiltFromSavedState= */ false, OWED, OWED));
    }

    @Test
    public void aParkedAddressOutranksADifferentOneArrivingAtLaunch() {
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH, /* rebuiltFromSavedState= */ false, OWED, ARRIVING));
    }

    @Test
    public void aParkedAddressOutranksADifferentOneArrivingWhileRunning() {
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.WHILE_RUNNING,
                        /* rebuiltFromSavedState= */ false,
                        OWED,
                        ARRIVING));
    }

    /** An unrelated intent arriving is not a reason to forget what is owed. */
    @Test
    public void anIntentCarryingNothingLeavesADebtAlone() {
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.WHILE_RUNNING,
                        /* rebuiltFromSavedState= */ false,
                        OWED,
                        /* arriving= */ null));
        assertEquals(
                OWED,
                TaffyInboundArrival.owedAfter(
                        Delivery.LAUNCH,
                        /* rebuiltFromSavedState= */ false,
                        OWED,
                        /* arriving= */ null));
    }

    /**
     * The whole table, written out rather than computed.
     *
     * <p>Sixteen rows: two deliveries, rebuilt or not, a debt or none, an address arriving or none.
     * The expected value of each row is a literal, so this test cannot agree with the
     * implementation by sharing its arithmetic — which is exactly how the defect it was written for
     * survived: the rule was one expression, and one expression cannot disagree with itself.
     */
    @Test
    public void everyCombinationAnswersTheRule() {
        // delivery, rebuilt, owed, arriving, expected
        assertRow(Delivery.LAUNCH, false, null, null, null);
        assertRow(Delivery.LAUNCH, false, null, ARRIVING, ARRIVING);
        assertRow(Delivery.LAUNCH, false, OWED, null, OWED);
        assertRow(Delivery.LAUNCH, false, OWED, ARRIVING, OWED);
        assertRow(Delivery.LAUNCH, true, null, null, null);
        assertRow(Delivery.LAUNCH, true, null, ARRIVING, null);
        assertRow(Delivery.LAUNCH, true, OWED, null, OWED);
        assertRow(Delivery.LAUNCH, true, OWED, ARRIVING, OWED);
        assertRow(Delivery.WHILE_RUNNING, false, null, null, null);
        assertRow(Delivery.WHILE_RUNNING, false, null, ARRIVING, ARRIVING);
        assertRow(Delivery.WHILE_RUNNING, false, OWED, null, OWED);
        assertRow(Delivery.WHILE_RUNNING, false, OWED, ARRIVING, OWED);
        assertRow(Delivery.WHILE_RUNNING, true, null, null, null);
        assertRow(Delivery.WHILE_RUNNING, true, null, ARRIVING, ARRIVING);
        assertRow(Delivery.WHILE_RUNNING, true, OWED, null, OWED);
        assertRow(Delivery.WHILE_RUNNING, true, OWED, ARRIVING, OWED);
    }

    private static void assertRow(
            Delivery delivery,
            boolean rebuiltFromSavedState,
            String owed,
            String arriving,
            String expected) {
        assertEquals(
                delivery + " rebuilt=" + rebuiltFromSavedState + " owed=" + owed + " arriving="
                        + arriving,
                expected,
                TaffyInboundArrival.owedAfter(delivery, rebuiltFromSavedState, owed, arriving));
    }
}
