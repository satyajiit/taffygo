// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

@RunWith(BaseRobolectricTestRunner.class)
public class TaffyManualGestureGateTest {
    private final long[] mNow = {100};
    private final TaffyManualGestureGate mGate = new TaffyManualGestureGate(() -> mNow[0]);

    @Test
    public void proofIsTabBoundAndSingleUse() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 90);

        assertTrue(mGate.consume(7));
        assertFalse(mGate.consume(7));
    }

    @Test
    public void crossTabClaimConsumesProofWithoutAuthorizing() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 90);

        assertFalse(mGate.consume(8));
        assertFalse(mGate.consume(7));
    }

    @Test
    public void staleAndFutureDatedProofsFailClosed() {
        mGate.record(
                /* tabId= */ 7,
                /* occurredAtMillis= */ mNow[0] - TaffyManualGestureGate.MAX_AGE_MILLIS - 1);
        assertFalse(mGate.consume(7));

        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ mNow[0] + 1);
        assertFalse(mGate.consume(7));
    }

    @Test
    public void clearRevokesProofWithoutClosingFutureManualInput() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 90);
        mGate.clear();
        assertFalse(mGate.consume(7));

        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 100);
        assertTrue(mGate.consume(7));
    }

    @Test
    public void closeRevokesAndPreventsNewProofs() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 90);
        mGate.close();
        assertFalse(mGate.consume(7));

        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 100);
        assertFalse(mGate.consume(7));
    }
}
