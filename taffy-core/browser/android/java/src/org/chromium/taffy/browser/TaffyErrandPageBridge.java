// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.chromium.chrome.browser.tab.Tab;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * The one browser-side fact screen SCR-110 needs that Java cannot state:
 * this tab's pages are never written to the visit store.
 *
 * <p>An errand page carries a vendor's authorization address, and that address
 * carries the flow's {@code state} value and its PKCE challenge — and, for
 * several vendors, the account the person is signing in as. None of that may
 * become a visit a person's history screen lists or an address-bar suggestion
 * an unrelated tab offers back to them later.
 *
 * <p>The mechanism is removal, not a filter. Chromium attaches its
 * {@code HistoryTabHelper} to every tab's {@code WebContents} when the tab is
 * built, and that helper is the only thing in the browser process that writes
 * a page, a title, a language or a password state into the visit store for a
 * tab. Taking it off the errand page's {@code WebContents} removes all of them
 * at once, before the first navigation is asked for, rather than gating one
 * call and leaving the rest to be found later. Every remaining reader of that
 * helper in the browser already answers null safely.
 *
 * <p>This is a one-way act on one {@code WebContents}, and there is no putting
 * it back: an errand page's tab is closed when the errand ends and is never
 * handed to anything that browses.
 */
public final class TaffyErrandPageBridge {

  /**
   * Stops this tab writing history, permanently. Returns whether it took —
   * the caller must refuse to open the errand page when it did not, because a
   * false here means the authorization address would be recorded.
   */
  public static boolean keepOutOfHistory(Tab tab) {
    return tab != null && TaffyErrandPageBridgeJni.get().keepOutOfHistory(tab);
  }

  private TaffyErrandPageBridge() {}

  @NativeMethods
  interface Natives {
    boolean keepOutOfHistory(@JniType("TabAndroid*") Tab tab);
  }
}
