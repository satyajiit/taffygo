// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.content;

import android.view.SurfaceHolder;

/**
 * A surface callback that withholds a size the compositor is not allowed to be given.
 *
 * <p><b>The crash this exists to stop.</b> On 2026-08-21 the browser's privileged process aborted
 * twice, at 10:49:52 and at 12:31:28, both times on the {@code VizCompositorTh} thread with
 * {@code [FATAL:components/viz/service/display/display.cc:456] DCHECK failed:
 * !clamped_size_px.IsEmpty()}. The two stacks are identical frame for frame, and the symbolized
 * top of both is {@code viz::Display::Resize} reached over the {@code DisplayPrivate} Mojo
 * interface. Sixty milliseconds before each abort the platform delivered
 * {@code surfaceChanged -- format=4 w=1268 h=0} to this view's {@code SurfaceView}. Nothing else in
 * ten hours of that device's log carries a zero-sized {@code surfaceChanged}, and nothing else in
 * the tree calls the function that aborted.
 *
 * <p><b>Why the size reaches the compositor at all.</b> There is exactly one path and it is a
 * straight line with no other caller:
 * {@code ContentViewRenderView}'s {@code SurfaceHolder.Callback} hands the width and height
 * straight to {@code ContentViewRenderView::SurfaceChanged}, which calls
 * {@code compositor_->SetWindowBounds(gfx::Size(width, height))}; {@code CompositorImpl} forwards
 * that to {@code display_private_->Resize(size)}; and {@code Display::Resize} early-returns only
 * when the new size equals the one it already has. An empty size is therefore harmless on a display
 * that has never been sized and fatal on one that has — which is why this appears as a crash on
 * resume rather than on launch.
 *
 * <p><b>Why a zero height is TaffyGo's to produce and TaffyGo's to absorb.</b> Screen SCR-101 gives
 * the page {@code Modifier.weight(1f)} in a {@code Column}, so the page's height is whatever the
 * chrome around it leaves. Compose clamps a weighted child at zero rather than going negative, so a
 * single layout pass in which the address bar, the action row and the assistant bar together claim
 * the whole column measures this view at zero height — legal Compose, legal Android, and a size no
 * compositor can be handed. Guarding here rather than pinning a minimum height on the layout is
 * what makes the rule hold for every screen that hosts a page later, including ones not written
 * yet.
 *
 * <p><b>Withholding is deferral, not loss.</b> The suppressed call carries two things and neither
 * is dropped. The surface handle is delivered by {@code SurfaceChanged}'s
 * {@code if (current_surface_format_ != format)} branch, and that field only advances when a call
 * actually arrives — so a format change withheld here is made by the next callback that carries a
 * real size, not skipped. The renderer's physical backing size is the other, and a zero backing
 * size tells it nothing true either. In both recorded crashes the real size followed within 100 ms.
 *
 * <p>{@code surfaceCreated} and {@code surfaceDestroyed} always pass through. They carry no size,
 * they are what native uses to start and stop presenting, and withholding either would strand the
 * compositor rather than protect it.
 */
public final class TaffyNonEmptySurfaceCallback implements SurfaceHolder.Callback {

    private final SurfaceHolder.Callback mDelegate;

    /**
     * Wraps {@code delegate}, which is upstream's own callback.
     *
     * @param delegate the callback every non-empty size is passed to unchanged.
     */
    public TaffyNonEmptySurfaceCallback(SurfaceHolder.Callback delegate) {
        assert delegate != null : "TaffyNonEmptySurfaceCallback needs a callback to guard";
        mDelegate = delegate;
    }

    /**
     * Whether a surface of this size may be given to the compositor.
     *
     * <p>The predicate is {@code gfx::Size::IsEmpty()} inverted, deliberately and to the letter:
     * that is the function {@code Display::Resize} asserts on, and a guard that disagreed with it
     * in either direction would either pass through a size that aborts or withhold one that is
     * fine. Both dimensions are checked because either one being zero makes the size empty.
     *
     * @param width the surface width in pixels, as the platform reported it.
     * @param height the surface height in pixels, as the platform reported it.
     * @return true when the size is one the compositor can be resized to.
     */
    public static boolean isDrawable(int width, int height) {
        return width > 0 && height > 0;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        mDelegate.surfaceCreated(holder);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (!isDrawable(width, height)) return;
        mDelegate.surfaceChanged(holder, format, width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        mDelegate.surfaceDestroyed(holder);
    }
}
