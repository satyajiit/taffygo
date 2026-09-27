// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_canonical_intent.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "crypto/sha2.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

void Field(std::vector<uint8_t>* out,
           uint8_t tag,
           base::span<const uint8_t> value) {
  out->push_back(tag);
  uint64_t length = value.size();
  for (size_t index = 0; index < 8u; ++index) {
    out->push_back(static_cast<uint8_t>(length >> (index * 8u)));
  }
  out->insert(out->end(), value.begin(), value.end());
}

void TextField(std::vector<uint8_t>* out, uint8_t tag, std::string_view value) {
  Field(out, tag, base::as_byte_span(value));
}

std::vector<uint8_t> Prefix() {
  constexpr std::string_view kPrefix = "taffy.action-intent.v1";
  return std::vector<uint8_t>(kPrefix.begin(), kPrefix.end());
}

std::vector<uint8_t> SearchIntent(std::string_view query) {
  constexpr std::string_view kHandle = "turn-7-call-0-search-query";
  std::vector<uint8_t> opaque;
  TextField(&opaque, 0u, kHandle);
  const std::array<uint8_t, 1> kind = {0u};
  Field(&opaque, 1u, kind);
  const std::array<uint8_t, 32> digest =
      crypto::SHA256Hash(base::as_byte_span(query));
  Field(&opaque, 2u, digest);

  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {1u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  Field(&intent, 2u, opaque);
  return intent;
}

std::vector<uint8_t> TabsOpenIntent(std::string_view address) {
  std::vector<uint8_t> optional_address = {1u};
  optional_address.insert(optional_address.end(), address.begin(),
                          address.end());
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {4u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  Field(&intent, 2u, optional_address);
  return intent;
}

std::vector<uint8_t> TaskTabIntent(uint8_t operation) {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation_bytes = {operation};
  Field(&intent, 0u, operation_bytes);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "browser-session-1");
  if (operation == 6u || operation == 7u) {
    TextField(&intent, 3u, "tab-owned");
    TextField(&intent, 4u, "frame-owned");
    TextField(&intent, 5u, "epoch-owned");
    const uint64_t revision = 9u;
    std::array<uint8_t, sizeof(revision)> revision_bytes = {};
    for (size_t index = 0; index < revision_bytes.size(); ++index) {
      revision_bytes[index] = static_cast<uint8_t>(revision >> (index * 8u));
    }
    Field(&intent, 6u, revision_bytes);
  }
  return intent;
}

std::vector<uint8_t> DomQueryIntent(
    std::optional<std::string_view> within = std::string_view("node-1"),
    std::optional<uint8_t> role = 1u,
    bool carries_text = true,
    std::optional<uint64_t> limit = 7u) {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {8u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");

  std::vector<uint8_t> optional_within = {
      static_cast<uint8_t>(within.has_value())};
  if (within) {
    optional_within.insert(optional_within.end(), within->begin(),
                           within->end());
  }
  Field(&intent, 2u, optional_within);

  const std::vector<uint8_t> optional_role =
      role ? std::vector<uint8_t>{1u, *role} : std::vector<uint8_t>{0u};
  Field(&intent, 3u, optional_role);

  std::vector<uint8_t> optional_text = {0u};
  if (carries_text) {
    std::vector<uint8_t> opaque;
    TextField(&opaque, 0u, "turn-7-call-0-dom-query-text");
    const std::array<uint8_t, 1> kind = {1u};
    Field(&opaque, 1u, kind);
    const std::array<uint8_t, 32> digest = {0x5au};
    Field(&opaque, 2u, digest);
    optional_text = {1u};
    Field(&optional_text, 0u, opaque);
  }
  Field(&intent, 4u, optional_text);

  std::vector<uint8_t> optional_limit = {
      static_cast<uint8_t>(limit.has_value())};
  if (limit) {
    for (size_t index = 0; index < sizeof(uint64_t); ++index) {
      optional_limit.push_back(static_cast<uint8_t>(*limit >> (index * 8u)));
    }
  }
  Field(&intent, 5u, optional_limit);
  return intent;
}

std::vector<uint8_t> LinkOpenIntent() {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {22u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "frame-1");
  TextField(&intent, 3u, "epoch-1");
  const uint64_t revision = 7u;
  std::array<uint8_t, sizeof(revision)> revision_bytes = {};
  for (size_t index = 0; index < revision_bytes.size(); ++index) {
    revision_bytes[index] = static_cast<uint8_t>(revision >> (index * 8u));
  }
  Field(&intent, 4u, revision_bytes);
  TextField(&intent, 5u, "node-1");
  const std::array<uint8_t, 1> tuple_origin = {0u};
  Field(&intent, 6u, tuple_origin);
  TextField(&intent, 7u, "https://example.test");
  return intent;
}

std::vector<uint8_t> FormInspectIntent() {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {12u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "form-1");
  return intent;
}

std::vector<uint8_t> FormSuppliedValueIntent(uint8_t operation,
                                             uint32_t index) {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation_bytes = {operation};
  Field(&intent, 0u, operation_bytes);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "field-1");
  TextField(&intent, 3u, "turn-4-values-1");
  std::array<uint8_t, sizeof(index)> index_bytes = {};
  for (size_t offset = 0; offset < index_bytes.size(); ++offset) {
    index_bytes[offset] = static_cast<uint8_t>(index >> (offset * 8u));
  }
  Field(&intent, 4u, index_bytes);
  return intent;
}

std::vector<uint8_t> FormToggleIntent(bool checked) {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {24u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "field-1");
  const std::array<uint8_t, 1> state = {static_cast<uint8_t>(checked)};
  Field(&intent, 3u, state);
  return intent;
}

std::vector<uint8_t> FormSubmitIntent() {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {14u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  TextField(&intent, 2u, "submit-1");
  return intent;
}

std::vector<uint8_t> SelectionReadIntent() {
  std::vector<uint8_t> intent = Prefix();
  const std::array<uint8_t, 1> operation = {17u};
  Field(&intent, 0u, operation);
  TextField(&intent, 1u, "tab-1");
  return intent;
}

TEST(CoreTaskCanonicalIntentTest, SearchBindsExactTransientBytesAndHandle) {
  const std::vector<uint8_t> intent = SearchIntent("tea & cake");
  EXPECT_TRUE(CanonicalSearchIntentMatches(
      intent, "tab-1", "turn-7-call-0-search-query", "tea & cake"));
  EXPECT_TRUE(CanonicalSearchQueryMatches(intent, "tab-1", "tea & cake"));
  EXPECT_FALSE(CanonicalSearchIntentMatches(
      intent, "tab-1", "turn-7-call-1-search-query", "tea & cake"));
  EXPECT_FALSE(CanonicalSearchQueryMatches(intent, "tab-1", "tea and cake"));
  EXPECT_FALSE(CanonicalSearchQueryMatches(intent, "tab-2", "tea & cake"));
}

TEST(CoreTaskCanonicalIntentTest, SearchRejectsTrailingCanonicalFields) {
  std::vector<uint8_t> intent = SearchIntent("bounded");
  const std::array<uint8_t, 1> invented = {0u};
  Field(&intent, 3u, invented);
  EXPECT_FALSE(CanonicalSearchQueryMatches(intent, "tab-1", "bounded"));
}

TEST(CoreTaskCanonicalIntentTest, TabsOpenBindsExactAddressAndContext) {
  constexpr std::string_view kAddress = "https://example.test/research?q=1";
  const std::vector<uint8_t> intent = TabsOpenIntent(kAddress);
  EXPECT_TRUE(
      CanonicalTabsOpenIntentMatches(intent, "tab-1", std::string(kAddress)));
  EXPECT_FALSE(CanonicalTabsOpenIntentMatches(
      intent, "tab-1", "https://example.test/research?q=2"));
  EXPECT_FALSE(
      CanonicalTabsOpenIntentMatches(intent, "tab-2", std::string(kAddress)));
}

TEST(CoreTaskCanonicalIntentTest, TaskTabsBindSessionAndExactDocument) {
  EXPECT_TRUE(CanonicalTaskTabListIntentMatches(TaskTabIntent(5u), "tab-1",
                                                "browser-session-1"));
  EXPECT_FALSE(CanonicalTaskTabListIntentMatches(TaskTabIntent(5u), "tab-1",
                                                 "browser-session-2"));
  EXPECT_TRUE(CanonicalTaskTabTargetIntentMatches(
      TaskTabIntent(6u), 6u, "tab-1", "browser-session-1", "tab-owned",
      "frame-owned", "epoch-owned", 9u));
  EXPECT_TRUE(CanonicalTaskTabTargetIntentMatches(
      TaskTabIntent(7u), 7u, "tab-1", "browser-session-1", "tab-owned",
      "frame-owned", "epoch-owned", 9u));
  EXPECT_FALSE(CanonicalTaskTabTargetIntentMatches(
      TaskTabIntent(6u), 6u, "tab-1", "browser-session-1", "tab-owned",
      "frame-owned", "epoch-owned", 10u));
}

TEST(CoreTaskCanonicalIntentTest, TaskTabsRejectZeroRevisionAndExtraFields) {
  std::vector<uint8_t> zero_revision = TaskTabIntent(6u);
  *(zero_revision.end() - sizeof(uint64_t)) = 0u;
  // The other seven revision bytes were already zero, so the target is now
  // explicitly unobserved and cannot become a page handle.
  EXPECT_FALSE(ReadCanonicalTaskTabIntent(zero_revision).has_value());

  std::vector<uint8_t> trailing = TaskTabIntent(5u);
  const std::array<uint8_t, 1> invented = {0u};
  Field(&trailing, 3u, invented);
  EXPECT_FALSE(ReadCanonicalTaskTabIntent(trailing).has_value());
}

TEST(CoreTaskCanonicalIntentTest,
     DomQueryValidatesFiltersWithoutProjectingAFilterNode) {
  EXPECT_TRUE(CanonicalDomQueryIntentMatches(DomQueryIntent(), "tab-1"));
  EXPECT_TRUE(CanonicalDomQueryIntentMatches(
      DomQueryIntent(std::nullopt, std::nullopt, false, std::nullopt),
      "tab-1"));
  EXPECT_FALSE(CanonicalDomQueryIntentMatches(DomQueryIntent(), "tab-2"));

  EXPECT_FALSE(CanonicalDomQueryIntentMatches(
      DomQueryIntent(std::string_view("node-1"), 8u, true, 7u), "tab-1"));
  EXPECT_FALSE(CanonicalDomQueryIntentMatches(
      DomQueryIntent(std::string_view("node-1"), 1u, true, 0u), "tab-1"));
  EXPECT_FALSE(CanonicalDomQueryIntentMatches(
      DomQueryIntent(std::string_view("node-1"), 1u, true, 129u), "tab-1"));
}

TEST(CoreTaskCanonicalIntentTest, DomQueryRejectsOpaqueKindAndTrailingFields) {
  std::vector<uint8_t> wrong_kind = Prefix();
  const std::array<uint8_t, 1> operation = {8u};
  Field(&wrong_kind, 0u, operation);
  TextField(&wrong_kind, 1u, "tab-1");
  const std::array<uint8_t, 1> absent = {0u};
  Field(&wrong_kind, 2u, absent);
  Field(&wrong_kind, 3u, absent);
  std::vector<uint8_t> opaque;
  TextField(&opaque, 0u, "turn-7-call-0-dom-query-text");
  const std::array<uint8_t, 1> search_kind = {0u};
  Field(&opaque, 1u, search_kind);
  const std::array<uint8_t, 32> digest = {0x5au};
  Field(&opaque, 2u, digest);
  std::vector<uint8_t> optional_text = {1u};
  Field(&optional_text, 0u, opaque);
  Field(&wrong_kind, 4u, optional_text);
  Field(&wrong_kind, 5u, absent);
  EXPECT_FALSE(CanonicalDomQueryIntentMatches(wrong_kind, "tab-1"));

  std::vector<uint8_t> trailing = DomQueryIntent();
  Field(&trailing, 6u, absent);
  EXPECT_FALSE(CanonicalDomQueryIntentMatches(trailing, "tab-1"));
}

TEST(CoreTaskCanonicalIntentTest, ExactReadToolsBindOnlyTheirNamedTarget) {
  EXPECT_TRUE(CanonicalFormInspectIntentMatches(FormInspectIntent(), "tab-1",
                                                "form-1"));
  EXPECT_FALSE(CanonicalFormInspectIntentMatches(FormInspectIntent(), "tab-1",
                                                 "form-2"));
  EXPECT_FALSE(CanonicalFormInspectIntentMatches(FormInspectIntent(), "tab-2",
                                                 "form-1"));
  EXPECT_TRUE(
      CanonicalSelectionReadIntentMatches(SelectionReadIntent(), "tab-1"));
  EXPECT_FALSE(
      CanonicalSelectionReadIntentMatches(SelectionReadIntent(), "tab-2"));

  std::vector<uint8_t> widened = SelectionReadIntent();
  TextField(&widened, 2u, "invented-node");
  EXPECT_FALSE(CanonicalSelectionReadIntentMatches(widened, "tab-1"));
}

TEST(CoreTaskCanonicalIntentTest,
     FormActionsBindExactTargetAndContentFreeInput) {
  const std::vector<uint8_t> fill = FormSuppliedValueIntent(13u, 2u);
  EXPECT_TRUE(CanonicalFormSuppliedValueIntentMatches(
      fill, 13u, "tab-1", "field-1", "turn-4-values-1", 2u));
  EXPECT_FALSE(CanonicalFormSuppliedValueIntentMatches(
      fill, 13u, "tab-1", "field-2", "turn-4-values-1", 2u));
  EXPECT_FALSE(CanonicalFormSuppliedValueIntentMatches(
      fill, 13u, "tab-1", "field-1", "turn-4-values-1", 3u));
  EXPECT_FALSE(CanonicalFormSuppliedValueIntentMatches(
      fill, 13u, "tab-1", "field-1", "turn-5-values-1", 2u));
  EXPECT_FALSE(CanonicalFormSuppliedValueIntentMatches(
      fill, 23u, "tab-1", "field-1", "turn-4-values-1", 2u));

  const std::vector<uint8_t> select = FormSuppliedValueIntent(23u, 1u);
  EXPECT_TRUE(CanonicalFormSuppliedValueIntentMatches(
      select, 23u, "tab-1", "field-1", "turn-4-values-1", 1u));
  EXPECT_FALSE(CanonicalFormSuppliedValueIntentMatches(
      select, 13u, "tab-1", "field-1", "turn-4-values-1", 1u));

  EXPECT_TRUE(CanonicalFormToggleIntentMatches(FormToggleIntent(true), "tab-1",
                                               "field-1", true));
  EXPECT_FALSE(CanonicalFormToggleIntentMatches(FormToggleIntent(true), "tab-1",
                                                "field-1", false));
  EXPECT_FALSE(CanonicalFormToggleIntentMatches(FormToggleIntent(false),
                                                "tab-2", "field-1", false));

  EXPECT_TRUE(CanonicalFormSubmitIntentMatches(FormSubmitIntent(), "tab-1",
                                               "submit-1"));
  EXPECT_FALSE(
      CanonicalFormSubmitIntentMatches(FormSubmitIntent(), "tab-1", "form-1"));
}

TEST(CoreTaskCanonicalIntentTest, LinkOpenBindsEveryObservedDocumentFact) {
  const std::vector<uint8_t> intent = LinkOpenIntent();
  EXPECT_TRUE(
      CanonicalLinkOpenIntentMatchesProjections(intent, "tab-1", "node-1"));
  EXPECT_TRUE(CanonicalLinkOpenIntentMatchesDocument(intent, "tab-1", "frame-1",
                                                     "epoch-1", 7u, "node-1",
                                                     "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-2", "frame-1", "epoch-1", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-1", "frame-2", "epoch-1", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-1", "frame-1", "epoch-2", 7u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-1", "frame-1", "epoch-1", 8u, "node-1",
      "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-1", "frame-1", "epoch-1", 7u, "node-2",
      "https://example.test"));
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      intent, "tab-1", "frame-1", "epoch-1", 7u, "node-1",
      "https://other.test"));
}

TEST(CoreTaskCanonicalIntentTest, LinkOpenRejectsUnknownFieldsAndOriginKinds) {
  std::vector<uint8_t> trailing = LinkOpenIntent();
  const std::array<uint8_t, 1> invented = {0u};
  Field(&trailing, 8u, invented);
  EXPECT_FALSE(ReadCanonicalLinkOpenHandle(trailing));

  std::vector<uint8_t> opaque = LinkOpenIntent();
  // Prefix, then the operation, tab, frame, epoch, revision, and node fields.
  // Rebuild rather than offset into variable-length fields so this test never
  // duplicates the parser's byte arithmetic.
  opaque = Prefix();
  const std::array<uint8_t, 1> operation = {22u};
  Field(&opaque, 0u, operation);
  TextField(&opaque, 1u, "tab-1");
  TextField(&opaque, 2u, "frame-1");
  TextField(&opaque, 3u, "epoch-1");
  const std::array<uint8_t, 8> revision = {7u};
  Field(&opaque, 4u, revision);
  TextField(&opaque, 5u, "node-1");
  const std::array<uint8_t, 1> opaque_kind = {1u};
  Field(&opaque, 6u, opaque_kind);
  TextField(&opaque, 7u, "opaque-1");
  const std::optional<CanonicalLinkOpenHandle> parsed =
      ReadCanonicalLinkOpenHandle(opaque);
  ASSERT_TRUE(parsed);
  EXPECT_TRUE(parsed->expected_origin_is_opaque);
  EXPECT_FALSE(CanonicalLinkOpenIntentMatchesDocument(
      opaque, "tab-1", "frame-1", "epoch-1", 7u, "node-1", "opaque-1"));
}

}  // namespace
}  // namespace taffy
