// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// JNI_OnLoad for `taffy_browsertests`.
//
// It is upstream's
// content/shell/android/browsertests_apk/content_browser_tests_jni_onload.cc
// with exactly one line changed: the ContentMainDelegate installed is
// TaffyGo's, so the renderer processes this binary starts create a
// TaffyRenderFrameObserver per frame. See taffy_content_renderer_client.h for
// why that has to happen here and cannot happen inside a test.
//
// A copy is what it costs. Referencing upstream's file directly — which this
// target did until 2026-08-19 — means taking upstream's delegate with it, and
// on Android that file is the only place the delegate is chosen:
// ContentTestLauncherDelegate::CreateContentMainDelegate is compiled out under
// `#if !BUILDFLAG(IS_ANDROID)`. The alternative was an upstream patch making
// the delegate injectable, and a copy of this size is cheaper than a line in
// the fork-debt budget for the same result.
//
// KEEP IN STEP WITH UPSTREAM ON A MILESTONE REBASE. The message-pump override
// and the OnJNIOnLoadInit ordering below are load bearing and are upstream's,
// not TaffyGo's: the pump has to be replaced before base::TestSuite::Initialize
// runs, because that also sets a MessagePumpForUIFactory. Diff this against
// upstream's copy when the pin moves.

#include <memory>

#include "base/android/jni_android.h"
#include "base/android/library_loader/library_loader_hooks.h"
#include "base/message_loop/message_pump.h"
#include "taffy/test/taffy_browser_test_main_delegate.h"
#include "content/public/app/content_jni_onload.h"
#include "content/public/app/content_main.h"
#include "content/public/test/nested_message_pump_android.h"
#include "testing/android/native_test/native_test_launcher.h"

// Global scope, not an anonymous namespace: base/android/library_loader/
// library_loader_hooks.h declares this exact function and this is its
// definition. In a namespace it becomes a second, ambiguous overload.
bool NativeInitializationHook(base::android::LibraryProcessType process_type) {
  // Upstream's comment, and it still applies: this needs to be done before
  // base::TestSuite::Initialize() is called, as it also tries to set
  // MessagePumpForUIFactory.
  base::MessagePump::OverrideMessagePumpForUIFactory(
      []() -> std::unique_ptr<base::MessagePump> {
        return std::make_unique<content::NestedMessagePumpAndroid>();
      });

  if (!content::android::OnJNIOnLoadInit()) {
    return false;
  }

  // The one changed line.
  content::SetContentMainDelegate(new taffy::test::TaffyBrowserTestMainDelegate());
  return true;
}

// This is called by the VM when the shared library is first loaded.
JNI_EXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  base::android::InitVM(vm);
  base::android::SetNativeInitializationHook(NativeInitializationHook);
  return JNI_VERSION_1_4;
}
