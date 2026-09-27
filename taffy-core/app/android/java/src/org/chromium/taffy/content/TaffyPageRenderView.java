// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.content;

import android.content.Context;
import android.view.SurfaceHolder;
import android.view.View;

import org.chromium.components.embedder_support.view.ContentViewRenderView;
import org.chromium.content_public.browser.WebContents;

/**
 * Upstream's render view, plus the one state it can be in that upstream cannot say out loud: no
 * page at all.
 *
 * <p><b>Why this class exists.</b> A browser with every tab closed is drawing nothing, and
 * {@link ContentViewRenderView} is written for that — its own {@code setCurrentWebContents} guards
 * the sizing calls with {@code if (webContents != null)}, its {@code mWebContents} field is
 * declared {@code @Nullable}, and its native half spells the null case out:
 * {@code compositor_->SetRootLayer(web_contents ? ... : scoped_refptr<cc::slim::Layer>())} in
 * {@code components/embedder_support/android/view/content_view_render_view.cc}. What refuses null
 * is neither of those. It is the generated JNI stub: the {@code webContents} parameter of the
 * class's {@code Natives} interface carries no {@code @Nullable} under the class's
 * {@code @NullMarked}, so jni_zero asserts and takes the process down with
 * {@code Parameter "webContents" was null. Add @Nullable to it?}. That killed TaffyGo on the second
 * launch, and no other embedder had met it: {@code ShellManager} wraps the call in a null check and
 * {@code CastWebContentsScopes} destroys the view instead, so TaffyGo is the first caller to pass
 * null.
 *
 * <p><b>Why a subclass rather than a patch.</b> Annotating the stub is one line upstream, and one
 * line upstream is a line every milestone rebase pays for. Everything needed to express "no page"
 * downstream is already {@code protected} or {@code public} on this class, so the cost here is a
 * file TaffyGo owns instead of a debt against a file it does not.
 *
 * <p><b>Why clearing the field is not optional.</b> Simply declining to make the call leaves
 * {@code mWebContents} pointing at the {@code WebContents} of the tab that was just closed, and
 * {@link ContentViewRenderView#onSizeChanged} calls {@code setSize} on whatever is in that field.
 * {@code WebContentsImpl.setSize} opens with {@code checkNotDestroyed()}, so the next rotation or
 * window resize would trade a JNI assert at tab-close for an {@code IllegalStateException} later —
 * a worse bug, because it is further from its cause. {@link #showNoPage} clears the field where the
 * field lives.
 *
 * <p><b>And why the surface is hidden too.</b> Without the JNI call the compositor keeps the closed
 * page's layer as its root layer, so it would keep presenting the last frame of a page that is
 * gone. Hiding the {@code SurfaceView} fires {@code surfaceDestroyed}, which is what tells native
 * to stop presenting. That is upstream's own idiom rather than an invention here:
 * {@code SurfaceBridge.initialize} leaves the {@code SurfaceView} {@code GONE} and
 * {@code SurfaceBridge.connect} is what makes it {@code VISIBLE}, so "no surface yet" and "no
 * surface any more" are the same state expressed the same way.
 */
public class TaffyPageRenderView extends ContentViewRenderView {

    public TaffyPageRenderView(Context context) {
        super(context);
    }

    /**
     * Upstream's bridge, with the one size the compositor may not be given filtered out.
     *
     * <p>All of the reasoning lives on {@link TaffyNonEmptySurfaceCallback}, which is the class
     * that does the work; this is the only seam it can be installed through, because the callback
     * it guards is an anonymous class {@code ContentViewRenderView.onNativeLibraryLoaded}
     * constructs and hands straight to {@code SurfaceBridge.connect}. Wrapping it there costs no
     * upstream line.
     *
     * <p><b>This runs during {@code super(context)} and so may touch no field of this class.</b>
     * {@code ContentViewRenderView}'s constructor calls this before any subclass initializer has
     * run, which is why the returned bridge is a static type holding nothing.
     */
    @Override
    protected SurfaceBridge createSurfaceBridge() {
        return new NonEmptySurfaceBridge();
    }

    /** Upstream's bridge, connecting a guarded callback in place of the one it is handed. */
    private static final class NonEmptySurfaceBridge extends SurfaceBridge {
        @Override
        protected void connect(SurfaceHolder.Callback surfaceCallback) {
            // super stores what it is given and removes that same object in disconnect(), so the
            // wrapper is what is registered and the wrapper is what is unregistered.
            super.connect(new TaffyNonEmptySurfaceCallback(surfaceCallback));
        }
    }

    /**
     * Draws {@code webContents}, whatever was being drawn before.
     *
     * <p>The surface is made visible first because {@link #showNoPage} may have hidden it, and the
     * order does not matter to native: {@code setCurrentWebContents} calls {@code InitCompositor}
     * itself, and the surface arrives through {@code surfaceCreated} on the next traversal exactly
     * as it does on the first frame after {@code onNativeLibraryLoaded}.
     *
     * @param webContents the page to draw. Never null — {@link #showNoPage} is how nothing is said.
     */
    public void showPage(WebContents webContents) {
        assert webContents != null : "TaffyPageRenderView.showPage(null); call showNoPage()";
        getSurfaceView().setVisibility(View.VISIBLE);
        setCurrentWebContents(webContents);
    }

    /**
     * Draws nothing, and stops holding the page it was drawing.
     *
     * <p>Both statements are load-bearing and neither replaces the other — see this class's own
     * documentation. Idempotent, because "no page" is a state a browser can enter twice.
     */
    public void showNoPage() {
        // Declared `protected` by ContentViewRenderView, and this is the reason a field like that
        // is protected: the reference must not outlive the page, and only a subclass is in a
        // position to say so.
        mWebContents = null;
        getSurfaceView().setVisibility(View.GONE);
    }
}
