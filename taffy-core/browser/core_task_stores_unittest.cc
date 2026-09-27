// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_effect_action.h"
#include "taffy/browser/core_task_store_rows.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 5u;
constexpr uint64_t kRevision = 9u;
constexpr uint64_t kNow = 10'000u;
constexpr std::string_view kHandle = "turn-3-call-0-store-query";
constexpr std::string_view kWords = "aadhaar download";

void Field(std::vector<uint8_t>* out,
           uint8_t tag,
           base::span<const uint8_t> value) {
  out->push_back(tag);
  const uint64_t length = value.size();
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    out->push_back(static_cast<uint8_t>(length >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

void TextField(std::vector<uint8_t>* out, uint8_t tag, std::string_view value) {
  Field(out, tag, base::as_byte_span(value));
}

void ByteField(std::vector<uint8_t>* out, uint8_t tag, uint8_t value) {
  const std::array<uint8_t, 1> bytes = {value};
  Field(out, tag, bytes);
}

void U32Field(std::vector<uint8_t>* out, uint8_t tag, uint32_t value) {
  std::array<uint8_t, sizeof(uint32_t)> bytes = {};
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<uint8_t>(value >> (index * 8u));
  }
  Field(out, tag, bytes);
}

std::vector<uint8_t> OpaqueQuery(std::string_view handle,
                                 std::string_view words,
                                 uint8_t operand_kind = 7u) {
  std::vector<uint8_t> opaque;
  TextField(&opaque, 0u, handle);
  ByteField(&opaque, 1u, operand_kind);
  const std::array<uint8_t, crypto::kSHA256Length> digest =
      crypto::SHA256Hash(base::as_byte_span(words));
  Field(&opaque, 2u, digest);
  return opaque;
}

std::vector<uint8_t> StoreIntent(uint8_t kind,
                                 std::string_view tab_id,
                                 std::optional<std::vector<uint8_t>> query,
                                 std::optional<uint32_t> limit,
                                 uint8_t family = 251u) {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(kPrefix.begin(), kPrefix.end());
  ByteField(&intent, 0u, family);
  ByteField(&intent, 1u, kind);
  TextField(&intent, 2u, tab_id);
  uint8_t tag = 3u;
  if (query) {
    Field(&intent, tag++, *query);
  }
  if (limit) {
    U32Field(&intent, tag++, *limit);
  }
  return intent;
}

std::vector<uint8_t> SearchIntent(uint8_t kind, uint32_t limit) {
  return StoreIntent(kind, "tab-1", OpaqueQuery(kHandle, kWords), limit);
}

std::vector<uint8_t> ListIntent(uint8_t kind, uint32_t limit) {
  return StoreIntent(kind, "tab-1", std::nullopt, limit);
}

std::vector<uint8_t> OpenTabsIntent() {
  return StoreIntent(4u, "tab-1", std::nullopt, std::nullopt);
}

mojom::TaskEffectBindingPtr StoreBinding(
    mojom::TaskActionOperationKind operation,
    uint32_t limit) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "task-effect-operation", kGeneration, kRevision, 13'000u,
      "task-action-idempotency");
  binding->effect_id = "task-effect-1";
  binding->task_id = "task-1";
  binding->ordinal = 0u;
  binding->kind = mojom::TaskReducerEffectKind::kDispatchAction;
  binding->action = mojom::TaskActionEffect::New();
  binding->action->action_id = "action-1";
  binding->action->proposal_digest = std::string(64u, 'a');
  binding->action->idempotency_key = "task-action-idempotency";
  binding->action->capability_id = "capability-1";
  binding->action->dispatch_id = "dispatch-1";
  binding->action->document = mojom::TaskFrozenDocument::New(
      "frame-1", "epoch-1", 7u, "https://example.test", std::nullopt);
  auto& executable = binding->action->executable;
  executable = mojom::TaskExecutableAction::New();
  executable->action_class = mojom::PolicyActionClass::kProfileStoreRead;
  executable->operation_kind = operation;
  executable->input = mojom::TaskActionInput::New();
  executable->input->kind = mojom::TaskActionInputKind::kNone;
  executable->tab_id = "tab-1";
  executable->task_store = mojom::TaskStoreActionBinding::New();
  executable->task_store->limit = limit;
  binding->action->preconditions = {
      mojom::TaskActionPrecondition::kDocumentUnchanged,
      mojom::TaskActionPrecondition::kGraphRevisionAtLeast,
  };
  binding->action->postcondition =
      mojom::TaskActionPostcondition::kStoreRowsListed;
  switch (operation) {
    case mojom::TaskActionOperationKind::kHistorySearch:
      executable->tool_name = "history.search";
      executable->canonical_intent = SearchIntent(0u, limit);
      executable->operand_handle = std::string(kHandle);
      executable->task_store->query = std::string(kWords);
      break;
    case mojom::TaskActionOperationKind::kHistoryRecent:
      executable->tool_name = "history.recent";
      executable->canonical_intent = ListIntent(1u, limit);
      break;
    case mojom::TaskActionOperationKind::kBookmarksSearch:
      executable->tool_name = "bookmarks.search";
      executable->canonical_intent = SearchIntent(2u, limit);
      executable->operand_handle = std::string(kHandle);
      executable->task_store->query = std::string(kWords);
      break;
    case mojom::TaskActionOperationKind::kBookmarksList:
      executable->tool_name = "bookmarks.list";
      executable->canonical_intent = ListIntent(3u, limit);
      break;
    case mojom::TaskActionOperationKind::kOpenTabsList:
      executable->tool_name = "open_tabs.list";
      executable->canonical_intent = OpenTabsIntent();
      break;
    default:
      break;
  }
  return binding;
}

bool Valid(const mojom::TaskEffectBinding& binding) {
  return IsValidTaskActionEffect(*binding.action, binding, kNow);
}

TaskStoreEntry Entry(std::u16string title, std::string_view url) {
  return TaskStoreEntry{
      .title = std::move(title),
      .url = GURL(url),
      .when = base::Time::UnixEpoch() + base::Milliseconds(1'700'000'000'000),
  };
}

TEST(CoreTaskStoresTest, EachKindParsesToItsExactShapeAndNoOther) {
  const std::optional<CanonicalStoreIntent> search =
      ReadCanonicalStoreIntent(SearchIntent(0u, 10u));
  ASSERT_TRUE(search);
  EXPECT_EQ(search->kind, 0u);
  EXPECT_EQ(search->context_tab_id, "tab-1");
  EXPECT_EQ(search->operand_handle, std::string(kHandle));
  EXPECT_EQ(search->limit, 10u);
  ASSERT_TRUE(search->query_digest);
  EXPECT_EQ(search->query_digest->size(), crypto::kSHA256Length);

  const std::optional<CanonicalStoreIntent> list =
      ReadCanonicalStoreIntent(ListIntent(3u, 5u));
  ASSERT_TRUE(list);
  EXPECT_EQ(list->kind, 3u);
  EXPECT_FALSE(list->operand_handle);
  EXPECT_EQ(list->limit, 5u);

  const std::optional<CanonicalStoreIntent> tabs =
      ReadCanonicalStoreIntent(OpenTabsIntent());
  ASSERT_TRUE(tabs);
  EXPECT_EQ(tabs->kind, 4u);
  EXPECT_FALSE(tabs->limit);

  EXPECT_TRUE(CanonicalStoreIntentMatches(SearchIntent(2u, 1u), 2u, "tab-1"));
  EXPECT_FALSE(CanonicalStoreIntentMatches(SearchIntent(2u, 1u), 0u, "tab-1"));
  EXPECT_FALSE(CanonicalStoreIntentMatches(SearchIntent(2u, 1u), 2u, "tab-2"));
  EXPECT_FALSE(CanonicalStoreIntentMatches(SearchIntent(2u, 1u), 2u, ""));

  // A search kind without its words, a list kind carrying words, open tabs
  // carrying a cap, a cap outside 1..32, an unknown kind and another family.
  EXPECT_FALSE(ReadCanonicalStoreIntent(ListIntent(0u, 10u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(SearchIntent(1u, 10u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(ListIntent(4u, 10u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(ListIntent(1u, 0u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(ListIntent(1u, 33u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(SearchIntent(0u, 33u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(
      StoreIntent(5u, "tab-1", std::nullopt, std::nullopt)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(
      StoreIntent(4u, "tab-1", std::nullopt, std::nullopt, 253u)));
  // The operand must be a store query, not a page-search or memory operand.
  EXPECT_FALSE(ReadCanonicalStoreIntent(
      StoreIntent(0u, "tab-1", OpaqueQuery(kHandle, kWords, 0u), 10u)));
  EXPECT_FALSE(ReadCanonicalStoreIntent(
      StoreIntent(0u, "tab-1", OpaqueQuery(kHandle, kWords, 3u), 10u)));
}

TEST(CoreTaskStoresTest, ASearchBindsItsWordsByDigestAndHandleAndItsCap) {
  const std::vector<uint8_t> intent = SearchIntent(0u, 10u);
  EXPECT_TRUE(CanonicalStoreSearchIntentMatches(
      intent, 0u, "tab-1", std::string(kHandle), std::string(kWords), 10u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      intent, 0u, "tab-1", std::string(kHandle), "aadhaar", 10u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      intent, 0u, "tab-1", "other-handle", std::string(kWords), 10u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      intent, 0u, "tab-1", std::string(kHandle), std::string(kWords), 11u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      intent, 2u, "tab-1", std::string(kHandle), std::string(kWords), 10u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      intent, 0u, "tab-1", std::string(kHandle), std::string(), 10u));
  EXPECT_FALSE(CanonicalStoreListIntentMatches(intent, 0u, "tab-1", 10u));

  EXPECT_TRUE(CanonicalStoreListIntentMatches(ListIntent(1u, 5u), 1u, "tab-1",
                                              5u));
  EXPECT_FALSE(CanonicalStoreListIntentMatches(ListIntent(1u, 5u), 1u, "tab-1",
                                               6u));
  EXPECT_FALSE(CanonicalStoreSearchIntentMatches(
      ListIntent(1u, 5u), 1u, "tab-1", std::string(kHandle),
      std::string(kWords), 5u));
  // Open tabs freeze no cap; the binding must carry the wire's own.
  EXPECT_TRUE(CanonicalStoreListIntentMatches(OpenTabsIntent(), 4u, "tab-1",
                                              mojom::kMaxTaskStoreResults));
  EXPECT_FALSE(CanonicalStoreListIntentMatches(OpenTabsIntent(), 4u, "tab-1",
                                               31u));
}

TEST(CoreTaskStoresTest, AStoreActionIsValidOnlyInItsExactShape) {
  constexpr std::array operations = {
      mojom::TaskActionOperationKind::kHistorySearch,
      mojom::TaskActionOperationKind::kHistoryRecent,
      mojom::TaskActionOperationKind::kBookmarksSearch,
      mojom::TaskActionOperationKind::kBookmarksList,
  };
  for (const mojom::TaskActionOperationKind operation : operations) {
    EXPECT_TRUE(Valid(*StoreBinding(operation, 10u)));
    EXPECT_TRUE(Valid(*StoreBinding(operation, mojom::kMaxTaskStoreResults)));
  }
  EXPECT_TRUE(Valid(*StoreBinding(mojom::TaskActionOperationKind::kOpenTabsList,
                                  mojom::kMaxTaskStoreResults)));
  EXPECT_FALSE(Valid(
      *StoreBinding(mojom::TaskActionOperationKind::kOpenTabsList, 31u)));

  auto wrong_class =
      StoreBinding(mojom::TaskActionOperationKind::kHistoryRecent, 10u);
  wrong_class->action->executable->action_class =
      mojom::PolicyActionClass::kObservePage;
  EXPECT_FALSE(Valid(*wrong_class));

  auto wrong_postcondition =
      StoreBinding(mojom::TaskActionOperationKind::kHistoryRecent, 10u);
  wrong_postcondition->action->postcondition =
      mojom::TaskActionPostcondition::kTaskTabsListed;
  EXPECT_FALSE(Valid(*wrong_postcondition));

  auto zero_cap =
      StoreBinding(mojom::TaskActionOperationKind::kBookmarksList, 10u);
  zero_cap->action->executable->task_store->limit = 0u;
  EXPECT_FALSE(Valid(*zero_cap));

  auto cap_disagrees =
      StoreBinding(mojom::TaskActionOperationKind::kBookmarksList, 10u);
  cap_disagrees->action->executable->task_store->limit = 11u;
  EXPECT_FALSE(Valid(*cap_disagrees));

  auto words_on_a_listing =
      StoreBinding(mojom::TaskActionOperationKind::kHistoryRecent, 10u);
  words_on_a_listing->action->executable->task_store->query = "aadhaar";
  EXPECT_FALSE(Valid(*words_on_a_listing));

  auto no_words =
      StoreBinding(mojom::TaskActionOperationKind::kHistorySearch, 10u);
  no_words->action->executable->task_store->query.reset();
  EXPECT_FALSE(Valid(*no_words));

  auto other_words =
      StoreBinding(mojom::TaskActionOperationKind::kHistorySearch, 10u);
  other_words->action->executable->task_store->query = "aadhaar";
  EXPECT_FALSE(Valid(*other_words));

  auto no_handle =
      StoreBinding(mojom::TaskActionOperationKind::kBookmarksSearch, 10u);
  no_handle->action->executable->operand_handle.reset();
  EXPECT_FALSE(Valid(*no_handle));

  auto with_node =
      StoreBinding(mojom::TaskActionOperationKind::kOpenTabsList,
                   mojom::kMaxTaskStoreResults);
  with_node->action->executable->node_id = "node-1";
  EXPECT_FALSE(Valid(*with_node));

  auto two_bindings =
      StoreBinding(mojom::TaskActionOperationKind::kHistoryRecent, 10u);
  two_bindings->action->executable->task_tab =
      mojom::TaskTabActionBinding::New();
  two_bindings->action->executable->task_tab->browser_session_id =
      "browser-session-1";
  EXPECT_FALSE(Valid(*two_bindings));

  auto with_destination =
      StoreBinding(mojom::TaskActionOperationKind::kHistoryRecent, 10u);
  with_destination->action->executable->destination_address =
      "https://example.test/";
  EXPECT_FALSE(Valid(*with_destination));
}

TEST(CoreTaskStoresTest, ARowIsAReferenceAndNothingMore) {
  const mojom::TaskStoreRowPtr row = ProjectTaskStoreRow(Entry(
      u"  My\tAadhaar \n card ",
      "https://uidai.gov.in/en/my-aadhaar/get-aadhaar.html?token=s3cret#top"));
  ASSERT_TRUE(row);
  EXPECT_EQ(row->title, "My Aadhaar card");
  EXPECT_EQ(row->host, "uidai.gov.in");
  EXPECT_EQ(row->path, "/en/my-aadhaar/get-aadhaar.html");
  EXPECT_EQ(row->when_utc_ms, 1'700'000'000'000u);

  const mojom::TaskStoreRowPtr bare =
      ProjectTaskStoreRow(Entry(u"", "http://example.test"));
  ASSERT_TRUE(bare);
  EXPECT_EQ(bare->title, "");
  EXPECT_EQ(bare->path, "/");

  const mojom::TaskStoreRowPtr controls =
      ProjectTaskStoreRow(Entry(u"abc", "https://example.test/p"));
  ASSERT_TRUE(controls);
  EXPECT_EQ(controls->title, "abc");

  const mojom::TaskStoreRowPtr long_title = ProjectTaskStoreRow(
      Entry(std::u16string(700u, u'é'), "https://example.test/p"));
  ASSERT_TRUE(long_title);
  EXPECT_LE(long_title->title.size(), mojom::kMaxTaskStoreRowFieldBytes);
  EXPECT_TRUE(base::IsStringUTF8(long_title->title));

  EXPECT_FALSE(ProjectTaskStoreRow(Entry(u"t", "chrome://history")));
  EXPECT_FALSE(ProjectTaskStoreRow(Entry(u"t", "file:///tmp/a.pdf")));
  EXPECT_FALSE(ProjectTaskStoreRow(Entry(u"t", "not a url")));
  EXPECT_FALSE(ProjectTaskStoreRow(
      Entry(u"t", "https://example.test/" + std::string(600u, 'p'))));
}

TEST(CoreTaskStoresTest, AResultKeepsTheFirstRowsAndCountsTheRest) {
  std::vector<TaskStoreEntry> entries;
  for (size_t index = 0; index < 40u; ++index) {
    entries.push_back(
        Entry(u"page", "https://example.test/" + std::to_string(index)));
  }
  entries.insert(entries.begin() + 2, Entry(u"private", "chrome://settings"));

  const mojom::TaskStoreActionResultPtr ten = BuildTaskStoreResult(
      mojom::TaskActionOperationKind::kHistoryRecent, entries, 10u);
  ASSERT_TRUE(ten);
  EXPECT_EQ(ten->operation_kind,
            mojom::TaskActionOperationKind::kHistoryRecent);
  ASSERT_EQ(ten->rows.size(), 10u);
  EXPECT_EQ(ten->rows[0]->path, "/0");
  EXPECT_EQ(ten->rows[2]->path, "/2");
  EXPECT_EQ(ten->omitted, 31u);

  const mojom::TaskStoreActionResultPtr capped = BuildTaskStoreResult(
      mojom::TaskActionOperationKind::kBookmarksList, entries, 64u);
  ASSERT_TRUE(capped);
  EXPECT_EQ(capped->rows.size(), mojom::kMaxTaskStoreResults);
  EXPECT_EQ(capped->omitted, 41u - mojom::kMaxTaskStoreResults);

  const mojom::TaskStoreActionResultPtr none = BuildTaskStoreResult(
      mojom::TaskActionOperationKind::kOpenTabsList, {}, 32u);
  ASSERT_TRUE(none);
  EXPECT_TRUE(none->rows.empty());
  EXPECT_EQ(none->omitted, 0u);
}

}  // namespace
}  // namespace taffy
