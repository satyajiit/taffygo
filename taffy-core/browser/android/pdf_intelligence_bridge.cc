// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/pdf_intelligence_bridge.h"

#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/android/tab_android.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/android/pdf_intelligence_jni_headers/TaffyPdfIntelligenceBridge_jni.h"
#include "taffy/browser/page_pdf_observation_platform.h"

namespace taffy {
namespace {

class AndroidPdfTextRead {
 public:
  AndroidPdfTextRead(uint32_t maximum_pages,
                     uint32_t maximum_code_units_per_page,
                     uint32_t maximum_total_code_units,
                     PagePdfNativeTextCompletion completion)
      : maximum_pages_(maximum_pages),
        maximum_code_units_per_page_(maximum_code_units_per_page),
        maximum_total_code_units_(maximum_total_code_units),
        completion_(std::move(completion)) {}

  AndroidPdfTextRead(const AndroidPdfTextRead&) = delete;
  AndroidPdfTextRead& operator=(const AndroidPdfTextRead&) = delete;
  ~AndroidPdfTextRead() = default;

  bool AddPage(uint32_t page_index, std::u16string text, bool truncated) {
    if (page_index != pages_.size() || page_index >= maximum_pages_ ||
        text.size() > maximum_code_units_per_page_ ||
        total_code_units_ > maximum_total_code_units_ ||
        text.size() > maximum_total_code_units_ - total_code_units_) {
      return false;
    }
    total_code_units_ += text.size();
    source_truncated_ |= truncated;
    pages_.push_back({.text = std::move(text), .truncated = truncated});
    return true;
  }

  std::optional<PagePdfNativeTextResult> BuildResult(uint32_t page_count,
                                                     uint32_t inspected_pages,
                                                     bool source_truncated) {
    const uint32_t expected_without_source_budget =
        std::min(page_count, maximum_pages_);
    if (page_count == 0u || inspected_pages != pages_.size() ||
        inspected_pages > expected_without_source_budget ||
        (inspected_pages < expected_without_source_budget &&
         !source_truncated)) {
      return std::nullopt;
    }
    PagePdfNativeTextResult result;
    result.page_count = page_count;
    result.pages = std::move(pages_);
    result.truncated =
        source_truncated_ || source_truncated || inspected_pages < page_count;
    return result;
  }

  void Complete(std::optional<PagePdfNativeTextResult> result) {
    if (completion_) {
      std::move(completion_).Run(std::move(result));
    }
  }

 private:
  const uint32_t maximum_pages_;
  const uint32_t maximum_code_units_per_page_;
  const uint32_t maximum_total_code_units_;
  PagePdfNativeTextCompletion completion_;
  uint32_t total_code_units_ = 0u;
  bool source_truncated_ = false;
  std::vector<PagePdfNativePageText> pages_;
};

using PendingReads = std::map<int64_t, std::unique_ptr<AndroidPdfTextRead>>;

PendingReads& Reads() {
  static base::NoDestructor<PendingReads> reads;
  return *reads;
}

int64_t NextReadId() {
  static int64_t next_read_id = 1;
  CHECK_LT(next_read_id, std::numeric_limits<int64_t>::max());
  return next_read_id++;
}

void CancelJavaRead(int64_t read_id) {
  Java_TaffyPdfIntelligenceBridge_cancelRead(jni_zero::AttachCurrentThread(),
                                             static_cast<jlong>(read_id));
}

std::unique_ptr<AndroidPdfTextRead> TakeRead(int64_t read_id) {
  auto found = Reads().find(read_id);
  if (found == Reads().end()) {
    return nullptr;
  }
  std::unique_ptr<AndroidPdfTextRead> read = std::move(found->second);
  Reads().erase(found);
  return read;
}

void FailRead(int64_t read_id, bool cancel_java) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::unique_ptr<AndroidPdfTextRead> read = TakeRead(read_id);
  if (!read) {
    return;
  }
  if (cancel_java) {
    CancelJavaRead(read_id);
  }
  read->Complete(std::nullopt);
}

void CancelRead(int64_t read_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!TakeRead(read_id)) {
    return;
  }
  CancelJavaRead(read_id);
}

class AndroidPagePdfObservationPlatform final
    : public PagePdfObservationPlatform {
 public:
  AndroidPagePdfObservationPlatform() = default;
  ~AndroidPagePdfObservationPlatform() override = default;

  base::OnceClosure ReadNativeText(
      content::WebContents* web_contents,
      uint32_t maximum_pages,
      uint32_t maximum_code_units_per_page,
      uint32_t maximum_total_code_units,
      uint32_t maximum_wait_milliseconds,
      PagePdfNativeTextCompletion completion) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    const int64_t read_id = NextReadId();
    Reads().emplace(read_id,
                    std::make_unique<AndroidPdfTextRead>(
                        maximum_pages, maximum_code_units_per_page,
                        maximum_total_code_units, std::move(completion)));
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    const bool valid =
        tab && maximum_pages != 0u && maximum_code_units_per_page != 0u &&
        maximum_total_code_units != 0u && maximum_wait_milliseconds != 0u &&
        maximum_pages <=
            static_cast<uint32_t>(std::numeric_limits<jint>::max()) &&
        maximum_code_units_per_page <=
            static_cast<uint32_t>(std::numeric_limits<jint>::max()) &&
        maximum_total_code_units <=
            static_cast<uint32_t>(std::numeric_limits<jint>::max()) &&
        maximum_wait_milliseconds <=
            static_cast<uint32_t>(std::numeric_limits<jint>::max());
    const bool started =
        valid &&
        Java_TaffyPdfIntelligenceBridge_startRead(
            jni_zero::AttachCurrentThread(), tab->GetJavaObject(),
            static_cast<jlong>(read_id), static_cast<jint>(maximum_pages),
            static_cast<jint>(maximum_code_units_per_page),
            static_cast<jint>(maximum_total_code_units),
            static_cast<jint>(maximum_wait_milliseconds));
    if (!started) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&FailRead, read_id, false));
    }
    return base::BindOnce(&CancelRead, read_id);
  }
};

AndroidPagePdfObservationPlatform& AndroidPlatform() {
  static base::NoDestructor<AndroidPagePdfObservationPlatform> platform;
  return *platform;
}

}  // namespace

void InstallAndroidPdfIntelligenceBridge() {
  InstallPagePdfObservationPlatform(&AndroidPlatform());
}

static void JNI_TaffyPdfIntelligenceBridge_OnPageText(JNIEnv* env,
                                                      jlong read_id,
                                                      jint page_index,
                                                      std::u16string text,
                                                      jboolean truncated) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto found = Reads().find(static_cast<int64_t>(read_id));
  if (found == Reads().end()) {
    return;
  }
  if (page_index < 0 ||
      !found->second->AddPage(static_cast<uint32_t>(page_index),
                              std::move(text), truncated)) {
    FailRead(static_cast<int64_t>(read_id), true);
  }
}

static void JNI_TaffyPdfIntelligenceBridge_OnReadComplete(
    JNIEnv* env,
    jlong read_id,
    jboolean success,
    jint page_count,
    jint inspected_pages,
    jboolean source_truncated) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::unique_ptr<AndroidPdfTextRead> read =
      TakeRead(static_cast<int64_t>(read_id));
  if (!read) {
    return;
  }
  if (!success || page_count <= 0 || inspected_pages < 0) {
    read->Complete(std::nullopt);
    return;
  }
  read->Complete(read->BuildResult(static_cast<uint32_t>(page_count),
                                   static_cast<uint32_t>(inspected_pages),
                                   source_truncated));
}

DEFINE_JNI(TaffyPdfIntelligenceBridge)

}  // namespace taffy
