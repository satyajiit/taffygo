// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/history/history_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/android/errand_page_jni_headers/TaffyErrandPageBridge_jni.h"

namespace taffy {

// Takes Chromium's history recorder off one tab's WebContents.
//
// `HistoryTabHelper` is a `content::WebContentsUserData`, and it is the only
// object in the browser process that writes this tab's pages, titles,
// languages and password states into the profile's visit store. Removing it
// destroys it — its destructor only withdraws an observer registration — and
// every subsequent write path is gone rather than refused, which is what makes
// this a property of the surface instead of a gate somebody has to remember.
//
// The two remaining browser-process readers of the helper
// (`ChromePasswordManagerClient::…` and the helper's own
// `DidOpenRequestedURL`) both null-check before use, and the Java class that
// reaches it through JNI is only ever called from Chrome's own tabbed and
// Custom Tab activities, neither of which this product runs.
//
// Called before the errand page's first navigation is asked for, which is what
// makes the removal cover the authorization address itself and not only what
// follows it.
static jboolean JNI_TaffyErrandPageBridge_KeepOutOfHistory(JNIEnv* env,
                                                           TabAndroid* tab) {
  if (!tab) {
    return false;
  }
  content::WebContents* web_contents = tab->web_contents();
  if (!web_contents) {
    return false;
  }
  web_contents->RemoveUserData(HistoryTabHelper::UserDataKey());
  // Answer the state of the world rather than the fact that a call was made:
  // the caller refuses to open the page on false, so a removal that somehow
  // did not take must not read as one that did.
  return HistoryTabHelper::FromWebContents(web_contents) == nullptr;
}

DEFINE_JNI(TaffyErrandPageBridge)

}  // namespace taffy
