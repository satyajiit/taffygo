// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.content;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.view.SurfaceHolder;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.util.ArrayList;
import java.util.List;

/**
 * What the compositor is told about the surface's size, case by case, on a laptop.
 *
 * <p><b>Why this suite exists.</b> The browser's privileged process aborted twice on 2026-08-21 on
 * {@code DCHECK failed: !clamped_size_px.IsEmpty()}, sixty milliseconds after the platform reported
 * a zero-height surface. The decision that was missing — whether a size that cannot be drawn is
 * passed on — lived nowhere a test could reach it, so the first evidence it was wrong was a process
 * death on a phone. {@link TaffyNonEmptySurfaceCallback} is that decision written down; this is the
 * suite that reads it back.
 *
 * <p>The sequences below are transcribed from that device's log rather than invented, which is the
 * point of the last two tests: they are the two crashes, replayed against the guard.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyNonEmptySurfaceCallbackTest {

    // -----------------------------------------------------------------------
    // The predicate, which is Display::Resize's own assertion inverted.
    // -----------------------------------------------------------------------

    @Test
    public void aSurfaceWithWidthAndHeightCanBeDrawnInto() {
        assertTrue(TaffyNonEmptySurfaceCallback.isDrawable(1268, 1704));
    }

    @Test
    public void aSurfaceWithNoHeightCannotBeDrawnInto() {
        // The size actually delivered before both aborts.
        assertFalse(TaffyNonEmptySurfaceCallback.isDrawable(1268, 0));
    }

    @Test
    public void aSurfaceWithNoWidthCannotBeDrawnInto() {
        // gfx::Size::IsEmpty() is true when either dimension is zero, so the guard has to agree
        // about both or it passes through a size that aborts.
        assertFalse(TaffyNonEmptySurfaceCallback.isDrawable(0, 1704));
    }

    @Test
    public void aSurfaceWithNeitherCannotBeDrawnInto() {
        assertFalse(TaffyNonEmptySurfaceCallback.isDrawable(0, 0));
    }

    // -----------------------------------------------------------------------
    // What reaches upstream's callback.
    // -----------------------------------------------------------------------

    @Test
    public void aRealSizeIsPassedThroughUnchanged() {
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceChanged(null, 4, 1268, 1704);

        assertEquals(List.of("changed 4 1268x1704"), upstream.calls());
    }

    @Test
    public void aZeroHeightSizeNeverReachesTheCompositor() {
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceChanged(null, 4, 1268, 0);

        assertEquals(List.of(), upstream.calls());
    }

    @Test
    public void creationAndDestructionAlwaysPassThrough() {
        // Neither carries a size, and both are what native uses to start and stop presenting.
        // Withholding either would strand the compositor rather than protect it.
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceCreated(null);
        guarded.surfaceDestroyed(null);

        assertEquals(List.of("created", "destroyed"), upstream.calls());
    }

    // -----------------------------------------------------------------------
    // The two crashes, replayed.
    // -----------------------------------------------------------------------

    @Test
    public void theResumeThatAbortedAtTenFortyNineNowSkipsStraightToTheRealSize() {
        // Transcribed from the device log: surface recreated at the height it had before the
        // browser went to the background, one layout pass at zero, then the height it settles at.
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceCreated(null);
        guarded.surfaceChanged(null, 4, 1268, 1704);
        guarded.surfaceChanged(null, 4, 1268, 0);
        guarded.surfaceChanged(null, 4, 1268, 1000);

        assertEquals(
                List.of("created", "changed 4 1268x1704", "changed 4 1268x1000"), upstream.calls());
    }

    @Test
    public void theResumeThatAbortedAtTwelveThirtyOneNowSkipsStraightToTheRealSize() {
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceCreated(null);
        guarded.surfaceChanged(null, 4, 1268, 1489);
        guarded.surfaceChanged(null, 4, 1268, 0);
        guarded.surfaceChanged(null, 4, 1268, 785);

        assertEquals(
                List.of("created", "changed 4 1268x1489", "changed 4 1268x785"), upstream.calls());
    }

    @Test
    public void aFormatChangeWithheldWithAnEmptySizeArrivesWithTheNextRealOne() {
        // The one thing the suppressed call carried that matters: native takes the surface handle
        // on a format change, and that only happens on a call it actually receives. Withholding
        // defers it to the next real size rather than dropping it.
        RecordingCallback upstream = new RecordingCallback();
        SurfaceHolder.Callback guarded = new TaffyNonEmptySurfaceCallback(upstream);

        guarded.surfaceCreated(null);
        guarded.surfaceChanged(null, 4, 1268, 0);
        guarded.surfaceChanged(null, 4, 1268, 1826);

        assertEquals(List.of("created", "changed 4 1268x1826"), upstream.calls());
    }

    /** Upstream's callback, standing in for the anonymous one and writing down what it was told. */
    private static final class RecordingCallback implements SurfaceHolder.Callback {
        private final List<String> mCalls = new ArrayList<>();

        List<String> calls() {
            return mCalls;
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            mCalls.add("created");
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            mCalls.add("changed " + format + " " + width + "x" + height);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            mCalls.add("destroyed");
        }
    }
}
