// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/errand_download_test_page.h"

#include <sstream>
#include <vector>

#include "base/android/path_utils.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_prompt_status.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_path_reservation_tracker.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/browser/web_contents.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/errand_task_model_endpoint.h"
#include "taffy/test/recovery/task_pdf_handoff_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

void ConfigureErrandDownloadDirectory(Profile& profile,
                                      base::ScopedTempDir& directory) {
  base::ScopedAllowBlockingForTesting allow_files;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "taffy-test-pdf-handoff")) {
    base::FilePath downloads;
    ASSERT_TRUE(base::android::GetDownloadsDirectory(&downloads));
    ASSERT_TRUE(directory.CreateUniqueTempDirUnderPath(downloads));
  } else {
    ASSERT_TRUE(directory.CreateUniqueTempDir());
  }
  DownloadPrefs::FromBrowserContext(&profile)->SetDownloadPath(
      directory.GetPath());
  profile.GetPrefs()->SetBoolean(prefs::kPromptForDownload, false);
  profile.GetPrefs()->SetInteger(
      prefs::kPromptForDownloadAndroid,
      static_cast<int>(DownloadPromptStatus::DONT_SHOW));
}

void RestartErrandCore(CoreServiceManager& core,
                       const CoreApiStatusObserver& observer) {
  ASSERT_EQ(CoreServiceManager::Availability::kReady, core.availability());
  base::Process process;
  size_t matches = 0u;
  for (const auto& info :
       content::ServiceProcessHost::GetRunningProcessInfo()) {
    if (!info.IsService<core_service::mojom::TaffyCoreService>()) {
      continue;
    }
    ++matches;
    process = info.GetProcess().Duplicate();
  }
  ASSERT_EQ(1u, matches);
  ASSERT_TRUE(process.IsValid());
  const uint64_t generation = core.service_generation();
  // Keep the browser, page and profile alive; the new sandboxed process must
  // restore the accepted definition from the real durable journal.
  ASSERT_TRUE(process.Terminate(/*exit_code=*/1, /*wait=*/false));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return core.service_generation() == generation + 1u; }));
  base::test::TestFuture<bool> prepared;
  core.PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());
  ASSERT_EQ(generation + 1u, core.service_generation());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return observer.malformed_payload_seen() ||
           (observer.service_generation() == generation + 1u &&
            observer.availability() ==
                core_api::mojom::CoreAvailability::kReady);
  }));
  ASSERT_FALSE(observer.malformed_payload_seen());
  ASSERT_EQ(core_api::mojom::CoreAvailability::kReady, observer.availability());
}

// A small valid blank PDF, authored here rather than depending on a data file
// that Android's profile runner may not install. Byte offsets are calculated
// from the actual object bodies, and the download is checked byte for byte.
std::string ErrandFixturePdf() {
  std::string pdf = "%PDF-1.4\n";
  std::vector<size_t> offsets;
  for (const char* object :
       {"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
        "/Resources << >> >>\nendobj\n"}) {
    offsets.push_back(pdf.size());
    pdf += object;
  }
  const size_t xref = pdf.size();
  pdf += "xref\n0 4\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    pdf += base::StringPrintf("%010zu 00000 n \n", offset);
  }
  return pdf +
         base::StringPrintf(
             "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
             xref);
}

void ExpectErrandFixturePdf(const download::DownloadItem& file) {
  ASSERT_EQ(download::DownloadItem::COMPLETE, file.GetState());
  EXPECT_EQ("application/pdf", file.GetMimeType());
  base::ScopedAllowBlockingForTesting allow_files;
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(file.GetTargetFilePath(), &bytes));
  EXPECT_EQ(ErrandFixturePdf(), bytes);
}

void ExpectErrandTaskDownloads(CoreServiceManager& core,
                               content::DownloadManager& manager,
                               const std::string& first_task_id,
                               const std::string& replay_task_id) {
  download::SimpleDownloadManager::DownloadVector downloads;
  manager.GetAllDownloads(&downloads);
  ASSERT_EQ(2u, downloads.size());
  for (const auto& item : downloads) {
    ASSERT_NO_FATAL_FAILURE(ExpectErrandFixturePdf(*item));
    EXPECT_NE(
        core.CanOpenTaskDownloadForPerson(first_task_id, item->GetGuid()),
        core.CanOpenTaskDownloadForPerson(replay_task_id, item->GetGuid()));
  }
}

void MaybeOpenErrandTaskPdf(content::WebContents& contents,
                            CoreServiceManager& core,
                            const std::string& replay_task_id) {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          "taffy-test-pdf-handoff")) {
    return;
  }
  auto* profile = Profile::FromBrowserContext(contents.GetBrowserContext());
  download::SimpleDownloadManager::DownloadVector downloads;
  profile->GetDownloadManager()->GetAllDownloads(&downloads);
  download::DownloadItem* replay_file = nullptr;
  for (const auto& item : downloads) {
    if (core.CanOpenTaskDownloadForPerson(replay_task_id, item->GetGuid())) {
      ASSERT_FALSE(replay_file);
      replay_file = item;
    }
  }
  ASSERT_TRUE(replay_file);
  base::test::TestFuture<bool> opened;
  StartTaskPdfHandoff(profile, contents.GetTopLevelNativeWindow(),
                      replay_task_id, replay_file->GetGuid(),
                      opened.GetCallback());
  ASSERT_TRUE(opened.Get());
  LOG(INFO) << "[taffy_test_pdf_handoff] chooser_accepted";
  // The optional device acceptance run keeps the owner alive while the
  // installed viewer is selected and inspected. This proves no rendering by
  // itself: the separately retained device capture must show the loaded file.
  base::RunLoop inspect_viewer;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, inspect_viewer.QuitClosure(), base::Seconds(20));
  inspect_viewer.Run();
}

std::string ErrandDownloadDiagnostic(const download::DownloadItem* file) {
  if (!file) {
    return "native_download=missing-or-ambiguous";
  }
  std::ostringstream out;
  out << "native_download_state=" << static_cast<int>(file->GetState())
      << " received_bytes=" << file->GetReceivedBytes()
      << " total_bytes=" << file->GetTotalBytes()
      << " all_data_saved=" << file->AllDataSaved()
      << " target_present=" << !file->GetTargetFilePath().empty()
      << " paused=" << file->IsPaused() << " dangerous=" << file->IsDangerous()
      << " danger_type=" << static_cast<int>(file->GetDangerType())
      << " insecure_status="
      << static_cast<int>(file->GetInsecureDownloadStatus())
      << " interrupt_reason=" << static_cast<int>(file->GetLastReason())
      << " elapsed_ms="
      << (base::Time::Now() - file->GetStartTime()).InMilliseconds();
  const auto* validation = base::StatisticsRecorder::FindHistogram(
      "Download.PathValidationResult.UserDownload");
  if (validation) {
    const auto samples = validation->SnapshotSamples();
    out << " path_conflict_count="
        << samples->GetCount(
               static_cast<int>(download::PathValidationResult::CONFLICT))
        << " path_success_count="
        << samples->GetCount(
               static_cast<int>(download::PathValidationResult::SUCCESS))
        << " path_not_writable_count="
        << samples->GetCount(static_cast<int>(
               download::PathValidationResult::PATH_NOT_WRITABLE));
  } else {
    out << " path_validation=not-reached";
  }
  return out.str();
}

void WaitForErrandReplayCompletion(const CoreApiStatusObserver& observer,
                                   const std::string& first_task_id,
                                   content::DownloadManager* manager,
                                   const ErrandTaskModelEndpoint& endpoint,
                                   uint32_t first_task_model_count) {
  namespace api = core_api::mojom;
  const auto diagnostic = [&] {
    std::ostringstream out;
    const auto task = observer.task_other_than(first_task_id);
    out << "availability=" << static_cast<int>(observer.availability())
        << " malformed=" << observer.malformed_payload_seen()
        << " sequence=" << observer.state_sequence()
        << " replay_task=" << task.has_value()
        << " model_requests=" << endpoint.request_count()
        << " invalid_model_request=" << endpoint.invalid_request_seen();
    if (task) {
      out << " phase=" << static_cast<int>(task->phase)
          << " revision=" << task->revision << " failure="
          << (task->failure_code ? static_cast<int>(*task->failure_code) : -1)
          << " pending_approval=" << task->pending_action_id.has_value()
          << " handover=" << task->waiting_for_handover;
    }
    download::SimpleDownloadManager::DownloadVector files;
    manager->GetAllDownloads(&files);
    out << " native_downloads=" << files.size();
    // More rows already fail the fixture's exact count; diagnostics stay
    // bounded even when a faulty task creates unexpected files.
    for (size_t index = 0; index < files.size() && index < 4u; ++index) {
      out << " [" << index << ": " << ErrandDownloadDiagnostic(files[index])
          << "]";
    }
    return out.str();
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (observer.availability() != api::CoreAvailability::kReady ||
        observer.malformed_payload_seen() ||
        endpoint.request_count() != first_task_model_count) {
      return true;
    }
    const auto task = observer.task_other_than(first_task_id);
    return task && (task->phase == api::TaskPhase::kCompleted ||
                    task->phase == api::TaskPhase::kPartial ||
                    task->phase == api::TaskPhase::kFailed ||
                    task->phase == api::TaskPhase::kCancelled);
  })) << diagnostic();
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability())
      << diagnostic();
  ASSERT_FALSE(observer.malformed_payload_seen()) << diagnostic();
  ASSERT_EQ(first_task_model_count, endpoint.request_count()) << diagnostic();
  const auto task = observer.task_other_than(first_task_id);
  ASSERT_TRUE(task) << diagnostic();
  ASSERT_EQ(api::TaskPhase::kCompleted, task->phase) << diagnostic();
}

void DisableErrandFlowAndRestart(CoreServiceManager& core,
                                 core_api::mojom::TaffyProfileCoreApi& remote,
                                 const CoreApiStatusObserver& observer,
                                 const std::string& first_task_id) {
  namespace api = core_api::mojom;
  ASSERT_EQ(1u, observer.site_skills().size());
  const auto skill = observer.site_skills().front();
  const auto replay = observer.task_other_than(first_task_id);
  ASSERT_TRUE(replay);
  ASSERT_EQ(api::TaskPhase::kCompleted, replay->phase);
  base::test::TestFuture<api::CoreApiSubmissionStatus> disabled;
  remote.MutateSiteSkill(api::SiteSkillMutationKind::kSetEnabled,
                         skill.skill_id, skill.active_version, "", {}, {}, 0u,
                         false, disabled.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, disabled.Get());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return observer.site_skills().size() == 1u &&
           observer.site_skills().front().status ==
               api::SiteSkillStatusView::kDisabled;
  }));
  ASSERT_NO_FATAL_FAILURE(RestartErrandCore(core, observer));
  ASSERT_EQ(1u, observer.site_skills().size());
  EXPECT_EQ(skill.skill_id, observer.site_skills().front().skill_id);
  EXPECT_EQ(skill.active_version,
            observer.site_skills().front().active_version);
  EXPECT_EQ(api::SiteSkillStatusView::kDisabled,
            observer.site_skills().front().status);
  const auto restored_replay = observer.task_other_than(first_task_id);
  const auto restored_first = observer.task_other_than(replay->task_id);
  ASSERT_TRUE(restored_replay && restored_first);
  EXPECT_EQ(replay->task_id, restored_replay->task_id);
  EXPECT_EQ(first_task_id, restored_first->task_id);
  EXPECT_EQ(api::TaskPhase::kCompleted, restored_replay->phase);
  EXPECT_EQ(api::TaskPhase::kCompleted, restored_first->phase);
  EXPECT_FALSE(restored_replay->pending_action_id);
  EXPECT_FALSE(restored_replay->waiting_for_handover);
}

std::unique_ptr<net::test_server::HttpResponse> ServeErrandDownloadFixture(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  if (request.relative_url == "/start") {
    response->set_content(
        "<!doctype html><title>Document service</title>"
        "<main><h1>Document service</h1></main>");
  } else if (request.relative_url == "/download-document") {
    response->set_content(R"HTML(<!doctype html><title>Download document</title>
      <main><h1>Download document</h1><section id="entry">
      <label>Verification code <input id="code" type="password"
        autocomplete="one-time-code"></label>
      <button id="verify" onclick="document.getElementById('entry').innerHTML =
        '<a href=&quot;/document.pdf?grant=' +
        document.getElementById('code').value + '&quot;>Download</a>'">
        Verify</button></section></main>)HTML");
  } else if (request.relative_url ==
             "/document.pdf?grant=private-fixture-entry-42") {
    response->set_content_type("application/pdf");
    // Android publishes to MediaStore even with a temporary profile folder.
    // Isolate each response, including replay, from the ordinary duplicate
    // filename dialog without changing any download-manager behavior.
    response->AddCustomHeader(
        "Content-Disposition",
        "attachment; filename=taffy-errand-" +
            base::Uuid::GenerateRandomV4().AsLowercaseString() + ".pdf");
    response->set_content(ErrandFixturePdf());
  } else {
    return nullptr;
  }
  return response;
}

}  // namespace taffy::test
