// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_page_evidence.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"
#include "taffy/test/corpus/corpus_expected_field.h"
#include "taffy/test/corpus/corpus_fixture.h"
#include "taffy/test/corpus/corpus_manifest.h"

namespace taffy::test {
namespace {

constexpr std::string_view kAbsentSentinel = "ABSENT";

std::string Sha256(std::string_view value) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(value)));
}

std::string NormalizeGraphText(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool pending_space = false;
  for (char character : value) {
    if (base::IsAsciiWhitespace(character)) {
      pending_space = !normalized.empty();
      continue;
    }
    if (pending_space) {
      normalized.push_back(' ');
      pending_space = false;
    }
    normalized.push_back(base::ToLowerASCII(character));
  }
  return normalized;
}

std::vector<std::string> GraphTextSegments(const GraphPayload& graph) {
  std::vector<std::string> segments;
  for (const GraphPayloadNode& node : graph.nodes) {
    if (!node.name.empty() && !node.name_withheld) {
      segments.push_back(NormalizeGraphText(node.name));
    }
    for (const std::string& text : node.texts) {
      if (!text.empty()) {
        segments.push_back(NormalizeGraphText(text));
      }
    }
  }
  return segments;
}

bool GraphWithheldText(const GraphPayload& graph) {
  return std::ranges::any_of(graph.nodes, [](const GraphPayloadNode& node) {
    return node.name_withheld || node.text_withheld ||
           node.value_states_withheld;
  });
}

bool ContainsAsciiWord(std::string_view text, std::string_view word) {
  for (size_t at = text.find(word); at != std::string_view::npos;
       at = text.find(word, at + 1u)) {
    const bool starts_word =
        at == 0u || !base::IsAsciiAlphaNumeric(text[at - 1u]);
    const size_t end = at + word.size();
    const bool ends_word =
        end == text.size() || !base::IsAsciiAlphaNumeric(text[end]);
    if (starts_word && ends_word) {
      return true;
    }
  }
  return false;
}

bool SegmentExplicitlyDeclaresAbsence(std::string_view segment,
                                      std::string_view field_name) {
  const size_t dot = field_name.rfind('.');
  const std::string leaf = NormalizeGraphText(
      dot == std::string_view::npos ? field_name : field_name.substr(dot + 1u));
  if (leaf.empty() || !ContainsAsciiWord(segment, leaf)) {
    return false;
  }
  return ContainsAsciiWord(segment, "absent") ||
         segment.find("does not publish") != std::string_view::npos ||
         segment.find("not present") != std::string_view::npos ||
         segment.find("not available") != std::string_view::npos;
}

const CorpusExpectedField* FindExpectedField(std::string_view fixture_id,
                                             std::string_view field_name) {
  const CorpusFixture& fixture = CorpusManifest::Get().ById(fixture_id);
  const auto found =
      std::ranges::find_if(fixture.expected_semantic_fields,
                           [field_name](const CorpusExpectedField& candidate) {
                             return candidate.field == field_name;
                           });
  return found == fixture.expected_semantic_fields.end() ? nullptr : &*found;
}

}  // namespace

std::optional<TaskBenchmarkPageFieldEvidence>
ExtractTaskBenchmarkPageFieldEvidence(
    const TaskBenchmarkScenario::RequiredFact& fact,
    std::string_view observed_fixture,
    bool graph_complete,
    const GraphPayload& graph) {
  if (fact.evidence != "page-field" || !fact.source_fixture ||
      !fact.source_field || *fact.source_fixture != observed_fixture) {
    return std::nullopt;
  }
  const CorpusExpectedField* const expected =
      FindExpectedField(observed_fixture, *fact.source_field);
  if (!expected || expected->value.empty()) {
    return std::nullopt;
  }

  const std::vector<std::string> segments = GraphTextSegments(graph);
  bool observed = false;
  if (expected->value == kAbsentSentinel) {
    observed =
        graph_complete && !GraphWithheldText(graph) &&
        std::ranges::any_of(segments, [&](const std::string& segment) {
          return SegmentExplicitlyDeclaresAbsence(segment, expected->field);
        });
  } else {
    const std::string normalized_expected = NormalizeGraphText(expected->value);
    observed = !normalized_expected.empty() &&
               std::ranges::any_of(segments, [&](const std::string& segment) {
                 return segment.find(normalized_expected) != std::string::npos;
               });
  }
  if (!observed) {
    return std::nullopt;
  }
  return TaskBenchmarkPageFieldEvidence{.expected_value_sha256 =
                                            Sha256(expected->value)};
}

}  // namespace taffy::test
