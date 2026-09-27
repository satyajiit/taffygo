// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.browser;

import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * The address of the engine's attribution page, as the browser process names
 * it.
 *
 * <p>Decision 0206 has About open the third-party notices the package carries,
 * and that notice is an engine page, which decision 0154 keeps out of every
 * place where a string becomes a load. This bridge is how the one exception
 * stays bounded: it takes no argument, so nothing a person types, a page says
 * or a caller passes can reach it, and what it answers is
 * {@code kAttributionResourcePath} in {@code taffy_product_identity.cc}, the
 * only copy of that address in the tree.
 */
public final class TaffyAttributionNoticeBridge {

  /** The attribution page's address. Never empty in a product build. */
  public static String attributionResourcePath() {
    return TaffyAttributionNoticeBridgeJni.get().attributionResourcePath();
  }

  private TaffyAttributionNoticeBridge() {}

  @NativeMethods
  interface Natives {
    @JniType("std::string")
    String attributionResourcePath();
  }
}
