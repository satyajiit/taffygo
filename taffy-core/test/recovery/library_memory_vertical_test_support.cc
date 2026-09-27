// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/library_memory_vertical_test_support.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "content/public/test/url_loader_interceptor.h"
#include "taffy/test/recovery/task_model_reply_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

namespace api = core_api::mojom;

constexpr char kTaskModelBaseUrl[] = "https://library-memory-model.test/v1";
constexpr char kTaskModelRequestUrl[] =
    "https://library-memory-model.test/v1/chat/completions";

std::vector<std::string> ScriptedReplies(std::string workspace_id,
                                         uint64_t workspace_revision,
                                         std::string fact_id,
                                         std::string library_query,
                                         std::string memory_statement,
                                         std::string memory_query) {
  CHECK_LE(workspace_revision,
           static_cast<uint64_t>(std::numeric_limits<int>::max()));
  base::DictValue library_save;
  library_save.Set("workspace", std::move(workspace_id));
  library_save.Set("workspace_revision", static_cast<int>(workspace_revision));
  library_save.Set("fact", std::move(fact_id));
  library_save.Set("entry_revision", 0);
  base::DictValue memory_save;
  memory_save.Set("statement", std::move(memory_statement));
  memory_save.Set("scope", "all_tasks");
  base::DictValue library_search;
  library_search.Set("query", std::move(library_query));
  library_search.Set("limit", 8);
  base::DictValue memory_search;
  memory_search.Set("query", std::move(memory_query));
  memory_search.Set("limit", 8);
  std::vector<std::string> replies;
  replies.push_back(StreamedToolReply("call_library_save", "library.save",
                                      std::move(library_save)));
  replies.push_back(StreamedToolReply("call_memory_save", "memory.save",
                                      std::move(memory_save)));
  replies.push_back(StreamedToolReply("call_library_search", "library.search",
                                      std::move(library_search)));
  replies.push_back(StreamedToolReply("call_memory_search", "memory.search",
                                      std::move(memory_search)));
  replies.push_back(StreamedFinalReply("Saved and checked both places."));
  return replies;
}

void ExpectMemoryWorkspace(
    const std::optional<ObservedMemoryWorkspace>& expected,
    const std::optional<ObservedMemoryWorkspace>& actual) {
  ASSERT_EQ(expected.has_value(), actual.has_value());
  if (!expected) {
    return;
  }
  EXPECT_EQ(expected->workspace_id, actual->workspace_id);
  EXPECT_EQ(expected->display_name, actual->display_name);
}

}  // namespace

class LibraryMemoryTaskModelEndpoint::Impl final {
 public:
  Impl(std::string workspace_id,
       uint64_t workspace_revision,
       std::string fact_id,
       std::string library_query,
       std::string memory_statement,
       std::string memory_query)
      : replies_(ScriptedReplies(std::move(workspace_id),
                                 workspace_revision,
                                 std::move(fact_id),
                                 std::move(library_query),
                                 std::move(memory_statement),
                                 std::move(memory_query))),
        interceptor_(std::make_unique<content::URLLoaderInterceptor>(
            base::BindRepeating(&Impl::Intercept, base::Unretained(this)))) {}

  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    if (params->url_request.url != GURL(kTaskModelRequestUrl)) {
      return false;
    }
    const uint32_t ordinal =
        request_count_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const std::optional<std::string> content_type =
        params->url_request.headers.GetHeader("Content-Type");
    const bool request_shape_valid =
        params->url_request.method == "POST" &&
        params->url_request.request_body &&
        content_type == "application/json" &&
        !params->url_request.headers.HasHeader("Authorization");
    if (!request_shape_valid || ordinal > replies_.size()) {
      invalid_request_seen_.store(true, std::memory_order_relaxed);
    }
    const std::string& reply =
        replies_[std::min<size_t>(ordinal, replies_.size()) - 1u];
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 200 OK\nContent-Type: text/event-stream\n", reply,
        params->client.get(), std::nullopt, GURL(kTaskModelRequestUrl));
    return true;
  }

  uint32_t request_count() const {
    return request_count_.load(std::memory_order_relaxed);
  }

  bool invalid_request_seen() const {
    return invalid_request_seen_.load(std::memory_order_relaxed);
  }

 private:
  const std::vector<std::string> replies_;
  std::atomic<uint32_t> request_count_{0u};
  std::atomic<bool> invalid_request_seen_{false};
  std::unique_ptr<content::URLLoaderInterceptor> interceptor_;
};

LibraryMemoryTaskModelEndpoint::LibraryMemoryTaskModelEndpoint(
    std::string workspace_id,
    uint64_t workspace_revision,
    std::string fact_id,
    std::string library_query,
    std::string memory_statement,
    std::string memory_query)
    : impl_(std::make_unique<Impl>(std::move(workspace_id),
                                   workspace_revision,
                                   std::move(fact_id),
                                   std::move(library_query),
                                   std::move(memory_statement),
                                   std::move(memory_query))) {}

LibraryMemoryTaskModelEndpoint::~LibraryMemoryTaskModelEndpoint() = default;

std::string LibraryMemoryTaskModelEndpoint::base_url() const {
  return kTaskModelBaseUrl;
}

uint32_t LibraryMemoryTaskModelEndpoint::request_count() const {
  return impl_->request_count();
}

bool LibraryMemoryTaskModelEndpoint::invalid_request_seen() const {
  return impl_->invalid_request_seen();
}

std::optional<ObservedLibrarySearch> RequestObservedLibrarySearch(
    mojo::Remote<api::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_id,
    const std::string& query,
    uint64_t expected_revision,
    const std::string& expected_entry_id) {
  base::test::TestFuture<api::CoreApiSubmissionStatus> admission;
  (*facade)->SearchLibrary(request_id, query, 8u, admission.GetCallback());
  if (admission.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    ADD_FAILURE() << "The Library search was not accepted: " << request_id;
    return std::nullopt;
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedLibrarySearch>& search =
            observer->library().search;
        return search && search->request_id == request_id &&
               search->library_revision == expected_revision &&
               std::ranges::any_of(search->hits, [&](const auto& hit) {
                 return hit.entry_id == expected_entry_id;
               });
      })) {
    ADD_FAILURE() << "CoreStatus did not publish the Library search: "
                  << request_id;
    return std::nullopt;
  }
  EXPECT_EQ(query, observer->library().search->query);
  return observer->library().search;
}

std::optional<ObservedMemorySearch> RequestObservedMemorySearch(
    mojo::Remote<api::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_id,
    const std::string& query,
    uint64_t expected_revision,
    const std::string& expected_memory_id) {
  base::test::TestFuture<api::CoreApiSubmissionStatus> admission;
  (*facade)->SearchMemory(request_id, query, 8u, admission.GetCallback());
  if (admission.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    ADD_FAILURE() << "The Memory search was not accepted: " << request_id;
    return std::nullopt;
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedMemorySearch>& search =
            observer->memory().search;
        return search && search->request_id == request_id &&
               search->memory_revision == expected_revision &&
               std::ranges::find(search->memory_ids, expected_memory_id) !=
                   search->memory_ids.end();
      })) {
    ADD_FAILURE() << "CoreStatus did not publish the Memory search: "
                  << request_id;
    return std::nullopt;
  }
  EXPECT_EQ(query, observer->memory().search->query);
  return observer->memory().search;
}

void ExpectRestoredLibrary(const ObservedLibraryStatus& expected,
                           const ObservedLibraryStatus& actual) {
  EXPECT_EQ(expected.availability, actual.availability);
  EXPECT_EQ(expected.revision, actual.revision);
  ASSERT_EQ(expected.entries.size(), actual.entries.size());
  for (size_t index = 0u; index < expected.entries.size(); ++index) {
    const ObservedLibraryEntry& left = expected.entries[index];
    const ObservedLibraryEntry& right = actual.entries[index];
    EXPECT_EQ(left.entry_id, right.entry_id);
    EXPECT_EQ(left.revision, right.revision);
    EXPECT_EQ(left.collection_id, right.collection_id);
    EXPECT_EQ(left.collection_name, right.collection_name);
    EXPECT_EQ(left.source_workspace_id, right.source_workspace_id);
    EXPECT_EQ(left.source_workspace_revision, right.source_workspace_revision);
    EXPECT_EQ(left.source_fact_id, right.source_fact_id);
    EXPECT_EQ(left.field, right.field);
    EXPECT_EQ(left.original_value, right.original_value);
    EXPECT_EQ(left.correction, right.correction);
    EXPECT_EQ(left.kind, right.kind);
    EXPECT_EQ(left.captured_at_epoch_ms, right.captured_at_epoch_ms);
    EXPECT_EQ(left.last_checked_epoch_ms, right.last_checked_epoch_ms);
    EXPECT_EQ(left.has_conflict, right.has_conflict);
    ASSERT_EQ(left.sources.size(), right.sources.size());
    for (size_t source_index = 0u; source_index < left.sources.size();
         ++source_index) {
      EXPECT_EQ(left.sources[source_index].source_id,
                right.sources[source_index].source_id);
      EXPECT_EQ(left.sources[source_index].title,
                right.sources[source_index].title);
      EXPECT_EQ(left.sources[source_index].host,
                right.sources[source_index].host);
      EXPECT_EQ(left.sources[source_index].observed_at_epoch_ms,
                right.sources[source_index].observed_at_epoch_ms);
    }
  }
}

void ExpectRestoredMemory(const ObservedMemoryStatus& expected,
                          const ObservedMemoryStatus& actual) {
  EXPECT_EQ(expected.availability, actual.availability);
  EXPECT_EQ(expected.revision, actual.revision);
  ASSERT_EQ(expected.records.size(), actual.records.size());
  for (size_t index = 0u; index < expected.records.size(); ++index) {
    const ObservedMemoryRecord& left = expected.records[index];
    const ObservedMemoryRecord& right = actual.records[index];
    EXPECT_EQ(left.memory_id, right.memory_id);
    EXPECT_EQ(left.revision, right.revision);
    EXPECT_EQ(left.statement, right.statement);
    EXPECT_EQ(left.source_kind, right.source_kind);
    EXPECT_EQ(left.source_task_id, right.source_task_id);
    ExpectMemoryWorkspace(left.source_workspace, right.source_workspace);
    EXPECT_EQ(left.scope_kind, right.scope_kind);
    ExpectMemoryWorkspace(left.scope_workspace, right.scope_workspace);
    EXPECT_EQ(left.sensitivity, right.sensitivity);
    EXPECT_EQ(left.created_at_epoch_ms, right.created_at_epoch_ms);
    EXPECT_EQ(left.updated_at_epoch_ms, right.updated_at_epoch_ms);
    EXPECT_EQ(left.reviewed_at_epoch_ms, right.reviewed_at_epoch_ms);
    EXPECT_EQ(left.expires_at_epoch_ms, right.expires_at_epoch_ms);
  }
}

}  // namespace taffy::test
