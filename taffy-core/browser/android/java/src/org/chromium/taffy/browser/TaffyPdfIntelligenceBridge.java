// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;
import androidx.pdf.PdfDocument;
import androidx.pdf.content.PdfPageTextContent;
import androidx.pdf.view.PdfView;

import kotlin.coroutines.Continuation;
import kotlin.coroutines.CoroutineContext;
import kotlin.coroutines.EmptyCoroutineContext;
import kotlin.coroutines.intrinsics.IntrinsicsKt;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import org.chromium.chrome.browser.pdf.PdfPage;
import org.chromium.chrome.browser.tab.Tab;

import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Reads native text from the exact AndroidX document already shown by a PDF tab. */
public final class TaffyPdfIntelligenceBridge {
    private static final int MAXIMUM_CONCURRENT_READS = 8;
    private static final int MAXIMUM_TEXT_SEGMENTS_PER_PAGE = 4096;
    private static final int MAXIMUM_VIEW_NODES = 512;
    private static final long LOAD_RETRY_MILLISECONDS = 50L;
    private static final Handler UI_HANDLER = new Handler(Looper.getMainLooper());
    private static final Map<Long, Read> READS = new HashMap<>();

    private TaffyPdfIntelligenceBridge() {}

    @CalledByNative
    private static boolean startRead(
            Tab tab,
            long readId,
            int maximumPages,
            int maximumCodeUnitsPerPage,
            int maximumTotalCodeUnits,
            int maximumWaitMilliseconds) {
        if (Looper.myLooper() != Looper.getMainLooper()
                || tab == null
                || tab.isDestroyed()
                || !(tab.getNativePage() instanceof PdfPage)
                || readId <= 0
                || maximumPages <= 0
                || maximumCodeUnitsPerPage <= 0
                || maximumTotalCodeUnits <= 0
                || maximumWaitMilliseconds <= 0
                || READS.containsKey(readId)
                || READS.size() >= MAXIMUM_CONCURRENT_READS) {
            return false;
        }
        PdfPage page = (PdfPage) tab.getNativePage();
        Read read =
                new Read(
                        tab,
                        page,
                        readId,
                        maximumPages,
                        maximumCodeUnitsPerPage,
                        maximumTotalCodeUnits,
                        maximumWaitMilliseconds);
        READS.put(readId, read);
        read.tryAttachDocument();
        return true;
    }

    @CalledByNative
    private static void cancelRead(long readId) {
        Read read = READS.remove(readId);
        if (read != null) read.cancel();
    }

    private static @Nullable PdfView findPdfView(View view) {
        ArrayDeque<View> pending = new ArrayDeque<>();
        pending.add(view);
        int inspected = 0;
        while (!pending.isEmpty() && inspected++ < MAXIMUM_VIEW_NODES) {
            View current = pending.removeFirst();
            if (current instanceof PdfView) return (PdfView) current;
            if (!(current instanceof ViewGroup)) continue;
            ViewGroup group = (ViewGroup) current;
            for (int index = 0; index < group.getChildCount(); ++index) {
                if (pending.size() + inspected >= MAXIMUM_VIEW_NODES) return null;
                pending.addLast(group.getChildAt(index));
            }
        }
        return null;
    }

    private static final class Read {
        private final Tab mTab;
        private final PdfPage mExpectedPage;
        private final String mExpectedUrl;
        private final long mReadId;
        private final int mMaximumPages;
        private final int mMaximumCodeUnitsPerPage;
        private final int mMaximumTotalCodeUnits;
        private final long mDeadlineElapsedRealtime;

        private @Nullable PdfDocument mDocument;
        private int mPageCount;
        private int mNextPage;
        private int mTotalCodeUnits;
        private boolean mFinished;
        private boolean mSourceTruncated;

        Read(
                Tab tab,
                PdfPage expectedPage,
                long readId,
                int maximumPages,
                int maximumCodeUnitsPerPage,
                int maximumTotalCodeUnits,
                int maximumWaitMilliseconds) {
            mTab = tab;
            mExpectedPage = expectedPage;
            mExpectedUrl = expectedPage.getUrl();
            mReadId = readId;
            mMaximumPages = maximumPages;
            mMaximumCodeUnitsPerPage = maximumCodeUnitsPerPage;
            mMaximumTotalCodeUnits = maximumTotalCodeUnits;
            mDeadlineElapsedRealtime = SystemClock.elapsedRealtime() + maximumWaitMilliseconds;
        }

        void tryAttachDocument() {
            if (!isActive()) return;
            PdfDocument document;
            try {
                PdfView view = currentPdfView();
                document = view == null ? null : view.getPdfDocument();
            } catch (RuntimeException exception) {
                complete(false);
                return;
            }
            if (document == null) {
                if (SystemClock.elapsedRealtime() >= mDeadlineElapsedRealtime) {
                    complete(false);
                } else {
                    UI_HANDLER.postDelayed(this::tryAttachDocument, LOAD_RETRY_MILLISECONDS);
                }
                return;
            }
            int pageCount;
            try {
                pageCount = document.getPageCount();
            } catch (RuntimeException exception) {
                complete(false);
                return;
            }
            if (pageCount <= 0) {
                complete(false);
                return;
            }
            mDocument = document;
            mPageCount = pageCount;
            readNextPage();
        }

        void cancel() {
            mFinished = true;
        }

        private boolean isActive() {
            return !mFinished && READS.get(mReadId) == this;
        }

        private @Nullable PdfView currentPdfView() {
            if (mTab.isDestroyed()
                    || mTab.getNativePage() != mExpectedPage
                    || !mExpectedUrl.equals(mExpectedPage.getUrl())) {
                return null;
            }
            return findPdfView(mExpectedPage.mPdfCoordinator.getView());
        }

        private boolean isExactDocument() {
            try {
                PdfView view = currentPdfView();
                return view != null && mDocument != null && view.getPdfDocument() == mDocument;
            } catch (RuntimeException exception) {
                return false;
            }
        }

        private void readNextPage() {
            if (!isActive() || !isExactDocument()) {
                complete(false);
                return;
            }
            if (SystemClock.elapsedRealtime() >= mDeadlineElapsedRealtime) {
                complete(false);
                return;
            }
            int inspectedLimit = Math.min(mPageCount, mMaximumPages);
            if (mNextPage >= inspectedLimit) {
                mSourceTruncated |= mNextPage < mPageCount;
                complete(true);
                return;
            }
            if (mTotalCodeUnits >= mMaximumTotalCodeUnits) {
                mSourceTruncated = true;
                complete(true);
                return;
            }
            PdfDocument document = mDocument;
            if (document == null) {
                complete(false);
                return;
            }
            final int pageIndex = mNextPage;
            Continuation<PdfDocument.PdfPageContent> continuation =
                    new Continuation<PdfDocument.PdfPageContent>() {
                        @Override
                        public CoroutineContext getContext() {
                            return EmptyCoroutineContext.INSTANCE;
                        }

                        @Override
                        public void resumeWith(Object result) {
                            postPageResult(pageIndex, result);
                        }
                    };
            try {
                Object result = document.getPageContent(pageIndex, continuation);
                if (result != IntrinsicsKt.getCOROUTINE_SUSPENDED()) {
                    postPageResult(pageIndex, result);
                }
            } catch (RuntimeException exception) {
                complete(false);
            }
        }

        private void postPageResult(int pageIndex, Object result) {
            UI_HANDLER.post(() -> onPageResult(pageIndex, result));
        }

        private void onPageResult(int pageIndex, Object result) {
            if (!isActive()) return;
            if (pageIndex != mNextPage
                    || !isExactDocument()
                    || !(result instanceof PdfDocument.PdfPageContent)) {
                complete(false);
                return;
            }
            PageText pageText;
            try {
                pageText =
                        boundedPageText(
                                (PdfDocument.PdfPageContent) result,
                                Math.min(
                                        mMaximumCodeUnitsPerPage,
                                        mMaximumTotalCodeUnits - mTotalCodeUnits));
            } catch (RuntimeException exception) {
                complete(false);
                return;
            }
            mTotalCodeUnits += pageText.text.length();
            mSourceTruncated |= pageText.truncated;
            TaffyPdfIntelligenceBridgeJni.get()
                    .onPageText(mReadId, pageIndex, pageText.text, pageText.truncated);
            if (!isActive()) return;
            ++mNextPage;
            UI_HANDLER.post(this::readNextPage);
        }

        private PageText boundedPageText(PdfDocument.PdfPageContent content, int maximumCodeUnits) {
            StringBuilder text =
                    new StringBuilder(Math.min(maximumCodeUnits, mMaximumCodeUnitsPerPage));
            boolean truncated = false;
            List<PdfPageTextContent> textContents = content.getTextContents();
            int segmentLimit = Math.min(textContents.size(), MAXIMUM_TEXT_SEGMENTS_PER_PAGE);
            truncated |= segmentLimit < textContents.size();
            for (int index = 0; index < segmentLimit; ++index) {
                String part = textContents.get(index).getText();
                if (part == null || part.isEmpty()) continue;
                if (text.length() != 0) {
                    if (text.length() == maximumCodeUnits) {
                        truncated = true;
                        break;
                    }
                    text.append('\n');
                }
                int remaining = maximumCodeUnits - text.length();
                int end = Math.min(part.length(), remaining);
                if (end > 0
                        && end < part.length()
                        && Character.isHighSurrogate(part.charAt(end - 1))) {
                    --end;
                }
                text.append(part, 0, end);
                if (end < part.length()) {
                    truncated = true;
                    break;
                }
            }
            return new PageText(text.toString(), truncated);
        }

        private void complete(boolean success) {
            if (!isActive()) return;
            boolean exactSuccess = success && isExactDocument();
            READS.remove(mReadId);
            mFinished = true;
            TaffyPdfIntelligenceBridgeJni.get()
                    .onReadComplete(
                            mReadId,
                            exactSuccess,
                            exactSuccess ? mPageCount : 0,
                            exactSuccess ? mNextPage : 0,
                            mSourceTruncated);
        }
    }

    private static final class PageText {
        final String text;
        final boolean truncated;

        PageText(String text, boolean truncated) {
            this.text = text;
            this.truncated = truncated;
        }
    }

    @NativeMethods
    interface Natives {
        void onPageText(
                long readId,
                int pageIndex,
                @JniType("std::u16string") String text,
                boolean truncated);

        void onReadComplete(
                long readId,
                boolean success,
                int pageCount,
                int inspectedPages,
                boolean sourceTruncated);
    }
}
