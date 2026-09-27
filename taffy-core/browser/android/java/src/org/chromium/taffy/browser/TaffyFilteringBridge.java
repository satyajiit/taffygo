// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import androidx.annotation.Nullable;
import java.io.Closeable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * Browser-owned facts and commands of the filtering plane (decision 0076),
 * for one profile.
 *
 * <p>Facts are pulled, never pushed: {@code onChanged} says only that
 * <i>something</i> changed — a posture edit, a ruleset compile, a tab's
 * coalesced count publication — and the holder re-reads whatever it projects,
 * which is the same one-refresh shape {@code ChromiumBrowserMediator} already
 * has for tabs. The callback arrives on the browser's main thread.
 *
 * <p>Commands go through the native service rather than the preference store,
 * because the service owns the validation: a site exception names a host,
 * never a location, and the seam that refuses a bad one must be the same seam
 * every caller uses.
 */
public final class TaffyFilteringBridge implements Closeable {
  private final Runnable mOnChanged;
  private long mNativePtr;

  /**
   * Opens the profile's filtering seam. {@code onChanged} runs on the main
   * thread after any change worth re-reading.
   */
  public static TaffyFilteringBridge open(Profile profile, Runnable onChanged) {
    TaffyFilteringBridge bridge = new TaffyFilteringBridge(onChanged);
    bridge.mNativePtr = TaffyFilteringBridgeJni.get().init(bridge, profile);
    if (bridge.mNativePtr == 0) {
      throw new IllegalStateException("The filtering plane is unavailable");
    }
    return bridge;
  }

  private TaffyFilteringBridge(Runnable onChanged) {
    mOnChanged = onChanged;
  }

  /** Whether blocking is acting on this tab's committed page right now. */
  public boolean isActiveFor(Tab tab) {
    return mNativePtr != 0 &&
        TaffyFilteringBridgeJni.get().isActiveFor(mNativePtr, tab);
  }

  /**
   * Whether a person has allowed this tab's committed site, whatever the
   * master toggle says.
   *
   * <p>The host never crosses this seam. It is read from the tab's own
   * {@code WebContents} in C++ and answered as one boolean, so a private tab's
   * site cannot reach Java, a screenshot, or an accessibility dump by way of
   * this call (decision 0128).
   */
  public boolean isExceptedFor(Tab tab) {
    return mNativePtr != 0 &&
        TaffyFilteringBridgeJni.get().isExceptedFor(mNativePtr, tab);
  }

  /**
   * The blocked-request count of this tab's committed page. Coalesced by the
   * plane: it may jump, never lie, and it resets when a navigation commits.
   */
  public int blockedCountFor(Tab tab) {
    return mNativePtr == 0
        ? 0
        : TaffyFilteringBridgeJni.get().blockedCountFor(mNativePtr, tab);
  }

  /**
   * Publishes whatever the tab's coalescer is holding, for the moments the
   * number is about to be looked at: tab activation, the site sheet opening.
   */
  public void flushCountFor(Tab tab) {
    if (mNativePtr != 0) {
      TaffyFilteringBridgeJni.get().flushCountFor(mNativePtr, tab);
    }
  }

  /** The profile's master toggle, as the preference holds it. */
  public boolean isEnabled() {
    return mNativePtr != 0 &&
        TaffyFilteringBridgeJni.get().isEnabled(mNativePtr);
  }

  public void setEnabled(boolean enabled) {
    if (mNativePtr != 0) {
      TaffyFilteringBridgeJni.get().setEnabled(mNativePtr, enabled);
    }
  }

  /**
   * Records or removes one site exception. False means the seam refused the
   * host — empty, over the contract's bound, or carrying a scheme or path
   * where a host belongs — and recorded nothing.
   */
  public boolean setSiteException(String host, boolean allow) {
    return mNativePtr != 0 &&
        TaffyFilteringBridgeJni.get().setSiteException(mNativePtr, host, allow);
  }

  /** Every host the person has excepted, for screen SCR-206's list. */
  public String[] siteExceptions() {
    return mNativePtr == 0
        ? new String[0]
        : TaffyFilteringBridgeJni.get().siteExceptions(mNativePtr);
  }

  /**
   * Monotonic identity of the filtering posture projected by this profile.
   *
   * <p>Blocked-count publications share the bridge's one changed signal with
   * posture edits. A caller can compare this value before copying the complete
   * exception list through JNI, so a count-only update remains constant work.
   */
  public long postureRevision() {
    return mNativePtr == 0
        ? 0
        : TaffyFilteringBridgeJni.get().postureRevision(mNativePtr);
  }

  /** The lifetime blocked total, including what has not flushed yet. */
  public long blockedTotal() {
    return mNativePtr == 0
        ? 0
        : TaffyFilteringBridgeJni.get().blockedTotal(mNativePtr);
  }

  /**
   * Blocked requests in the current seven-day window, including what has
   * not flushed yet. Private-tab blocks are not in this number. Null means
   * no window has started. Zero is a counted zero; reading never mints a
   * window, and a missing window is not invented from {@link #blockedTotal()}.
   */
  public @Nullable Long blockedThisWeek() {
    if (mNativePtr == 0 || !hasWeekWindow()) {
      return null;
    }
    return TaffyFilteringBridgeJni.get().blockedThisWeek(mNativePtr);
  }

  /**
   * A lower bound on distinct document hosts that contributed a block in the
   * current week window. Identity retention is bounded, so this value stops
   * increasing at the browser's capacity. Null with {@link #blockedThisWeek()}
   * when no window has started.
   */
  public @Nullable Integer minimumSitesThisWeek() {
    if (mNativePtr == 0 || !hasWeekWindow()) {
      return null;
    }
    return TaffyFilteringBridgeJni.get().minimumSitesThisWeek(mNativePtr);
  }

  /** Whether a seven-day window has started. Reading does not mint one. */
  public boolean hasWeekWindow() {
    return mNativePtr != 0 &&
        TaffyFilteringBridgeJni.get().hasWeekWindow(mNativePtr);
  }

  @Override
  public void close() {
    long ptr = mNativePtr;
    if (ptr == 0) return;
    mNativePtr = 0;
    TaffyFilteringBridgeJni.get().destroy(ptr);
  }

  @CalledByNative
  private void onChanged() {
    if (mNativePtr != 0) mOnChanged.run();
  }

  @NativeMethods
  interface Natives {
    long init(TaffyFilteringBridge caller,
              @JniType("Profile*") Profile profile);

    void destroy(long bridgePtr);

    boolean isActiveFor(long bridgePtr, @JniType("TabAndroid*") Tab tab);

    boolean isExceptedFor(long bridgePtr, @JniType("TabAndroid*") Tab tab);

    int blockedCountFor(long bridgePtr, @JniType("TabAndroid*") Tab tab);

    void flushCountFor(long bridgePtr, @JniType("TabAndroid*") Tab tab);

    boolean isEnabled(long bridgePtr);

    void setEnabled(long bridgePtr, boolean enabled);

    boolean setSiteException(long bridgePtr,
                             @JniType("std::string") String host,
                             boolean allow);

    @JniType("std::vector<std::string>")
    String[] siteExceptions(long bridgePtr);

    long postureRevision(long bridgePtr);

    long blockedTotal(long bridgePtr);

    long blockedThisWeek(long bridgePtr);

    int minimumSitesThisWeek(long bridgePtr);

    boolean hasWeekWindow(long bridgePtr);
  }
}
