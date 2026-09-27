// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.content;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.Nullable;

import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;

/**
 * The two views a web page needs, in the order it needs them: pixels underneath, input on top.
 *
 * <p><b>What this is.</b> A plain {@link FrameLayout} with exactly two children. Child 0 is a
 * {@link TaffyPageRenderView} — upstream's own {@link android.view.SurfaceView} plus the
 * {@code content::Compositor} behind it, which is what actually draws the page. Child 1 is the
 * view the page's <i>input</i> arrives through: touch, IME, text selection and accessibility. The
 * two are separate objects upstream and they are separate children here, because the renderer
 * draws into a surface the window composites and the platform delivers events to a view in the
 * hierarchy, and nothing in Android joins those two facts for you.
 *
 * <p><b>Why {@code ContentViewRenderView} and not {@code CompositorViewHolder}.</b> Chrome's own
 * page host is {@code CompositorViewHolder}, and it cannot be used without Chrome's toolbar and
 * tab-switcher stack: it asserts a non-null {@code LayoutManager} and a non-null
 * {@code ToolbarThemeColorProvider} before it will initialize, its
 * {@code LayoutManagerImpl} reaches through {@code LayoutManagerHost} for the concrete
 * {@code BrowserControlsManager}, and its native half {@code DCHECK}s a {@code TabContentManager}.
 * Standing those up to draw one page would be adopting Chrome's interface through the back door,
 * which is the whole of what decision 0024 says TaffyGo does not do. {@code ThinWebView} was the
 * other candidate and was rejected on a narrower but worse ground: its {@code WindowAndroid}
 * overload sets {@code mEnablePermissionRequests = false}, which reaches
 * {@code set_web_contents_supports_permission_requests(false)} in native and would silently
 * disable permission prompts for every page in the product.
 *
 * <p>{@code ContentViewRenderView} touches no delegate and no tab helper — its native half only
 * sets the layer — so a {@code Tab} can own the {@code WebContents}, its {@code ContentView} and
 * its {@code TabViewAndroidDelegate} exactly as {@code TabImpl.setupContentView} does today, and
 * hand this class the two views without either side losing anything. That is what makes this the
 * page host for the tab model that follows rather than a fixture that has to be replaced by it.
 *
 * <p><b>This file names nothing from //chrome, and that is a structural rule rather than a
 * preference.</b> {@code //taffy/app/android/DEPS} rule 1 says the Android seam never
 * reaches into the embedder; the one declared exception is
 * {@code //taffy/app/android/shell/}, which is embedder assembly. A page host is a library,
 * so it lives here and takes a {@link WebContents} and a {@link View} from whoever owns them.
 * Needing a //chrome type here means the file is in the wrong directory, not that the rule needs
 * widening.
 *
 * <p><b>Z-order.</b> {@code ContentViewRenderView} puts its {@code SurfaceView} in media-overlay
 * mode, which is above other surfaces and still <i>behind the window</i>. So every pixel the
 * Compose interface paints composites over the page, which is the layering screen SCR-101 needs —
 * and equally, anything opaque an ancestor paints <i>after</i> this view in the same window covers
 * the page completely. Where this view sits in the tree is therefore a correctness question, not a
 * layout preference.
 */
public class TaffyPageHostView extends FrameLayout {

    /** Pixels. Added by {@link #initialize}, and the reason this class exists. */
    private static final int RENDER_VIEW_INDEX = 0;

    /** Input. Swapped by {@link #showPage} whenever the page being shown changes. */
    private static final int CONTENT_VIEW_INDEX = 1;

    private @Nullable TaffyPageRenderView mRenderView;

    private @Nullable View mContentView;

    public TaffyPageHostView(Context context) {
        super(context);
    }

    /**
     * Builds the compositor and attaches it. Requires the native library.
     *
     * <p><b>The order of the three statements below is asserted by upstream and is not
     * rearrangeable.</b> {@code ContentViewRenderView.onNativeLibraryLoaded} opens with
     * {@code assert !getSurfaceView().getHolder().getSurface().isValid()} — a surface that already
     * exists has no compositor behind it and never gets one, because the callback that would have
     * told native about it is installed at the end of that same method. Constructing the render
     * view leaves its {@code SurfaceView} {@code GONE}, and {@code connect()} is what makes it
     * visible, so calling {@code onNativeLibraryLoaded} before {@link #addView} is what keeps that
     * assertion true rather than what happens to make it true today. Upstream orders it the same
     * way in {@code ShellManager.setWindow}.
     *
     * @param window the activity's {@link WindowAndroid}; the compositor is bound to it.
     */
    public void initialize(WindowAndroid window) {
        assert mRenderView == null : "TaffyPageHostView.initialize called twice";
        TaffyPageRenderView renderView = new TaffyPageRenderView(getContext());
        renderView.onNativeLibraryLoaded(window);
        addView(
                renderView,
                RENDER_VIEW_INDEX,
                new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));
        mRenderView = renderView;
    }

    /** Whether {@link #initialize} has run. False before native initialization finishes. */
    public boolean isInitialized() {
        return mRenderView != null;
    }

    /**
     * Shows one page: its pixels through the compositor, its input through {@code contentView}.
     *
     * <p>Both arguments are nullable and they are nullable together — {@code (null, null)} is how
     * "no page" is said, which is the state a browser with every tab closed is actually in. A
     * caller that passes a {@link WebContents} without its view gets a page that draws and cannot
     * be touched, so the two are swapped in one call rather than in two.
     *
     * <p><b>"No page" goes through {@link TaffyPageRenderView#showNoPage} and never through
     * upstream's setter.</b> Passing null to {@code ContentViewRenderView.setCurrentWebContents}
     * kills the process at the generated JNI stub, and simply skipping the call would leave the
     * render view holding the closed page. That whole argument lives on
     * {@link TaffyPageRenderView}; what matters here is that this method's documented contract —
     * two nullable arguments, nullable together — is one this class actually keeps.
     *
     * <p>The content view is added at a fixed index rather than appended, so the render view stays
     * beneath it however many times the page is swapped. {@code ThinWebViewImpl} and
     * {@code CompositorViewHolder} both do the same thing for the same reason.
     *
     * @param webContents the page to draw, or null to draw nothing.
     * @param contentView the view its input arrives through, or null.
     */
    public void showPage(@Nullable WebContents webContents, @Nullable View contentView) {
        assert mRenderView != null : "TaffyPageHostView.showPage before initialize";
        if (mContentView != contentView) {
            if (mContentView != null) {
                removeView(mContentView);
                mContentView = null;
            }
            if (contentView != null) {
                // A Tab keeps its content view across activities, so it may still be attached
                // somewhere else. Re-parenting silently is the upstream behaviour; adding without
                // detaching first is an IllegalStateException.
                ViewGroup previousParent = (ViewGroup) contentView.getParent();
                if (previousParent != null) previousParent.removeView(contentView);
                addView(
                        contentView,
                        CONTENT_VIEW_INDEX,
                        new FrameLayout.LayoutParams(
                                FrameLayout.LayoutParams.MATCH_PARENT,
                                FrameLayout.LayoutParams.MATCH_PARENT));
                contentView.requestFocus();
                mContentView = contentView;
            }
        }
        if (webContents == null) {
            mRenderView.showNoPage();
        } else {
            mRenderView.showPage(webContents);
        }
    }

    /**
     * Releases the compositor. Idempotent, because an activity's teardown is not.
     *
     * <p>The content view is detached but not destroyed: this class never owned it. Whoever owns
     * the {@link WebContents} owns the view its input arrives through, and destroying it here would
     * take a live tab's view away from the tab.
     */
    public void destroy() {
        if (mContentView != null) {
            removeView(mContentView);
            mContentView = null;
        }
        if (mRenderView != null) {
            mRenderView.destroy();
            removeView(mRenderView);
            mRenderView = null;
        }
    }
}
