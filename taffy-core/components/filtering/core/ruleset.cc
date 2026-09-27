// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/ruleset.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/hash/hash.h"
#include "taffy/components/filtering/core/cosmetic_resources.h"
#include "taffy/components/filtering/core/scoped_adblock_string.h"
#include "taffy/third_party/adblock-rust/c_abi/include/taffy_adblock.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {

namespace proto = url_pattern_index::proto;

namespace {

TaffyAdblockStringView AdblockView(std::string_view value) {
  return TaffyAdblockStringView{value.data(), value.size()};
}

int ChecksumOf(const std::vector<uint8_t>& bytes) {
  return static_cast<int>(
      base::PersistentHash(base::span<const uint8_t>(bytes)));
}

const char* RequestTypeFor(proto::ElementType element_type) {
  switch (element_type) {
    case proto::ELEMENT_TYPE_SCRIPT:
      return "script";
    case proto::ELEMENT_TYPE_IMAGE:
      return "image";
    case proto::ELEMENT_TYPE_STYLESHEET:
      return "stylesheet";
    case proto::ELEMENT_TYPE_OBJECT:
    case proto::ELEMENT_TYPE_OBJECT_SUBREQUEST:
      return "object";
    case proto::ELEMENT_TYPE_SUBDOCUMENT:
      return "subdocument";
    case proto::ELEMENT_TYPE_FONT:
      return "font";
    case proto::ELEMENT_TYPE_MEDIA:
      return "media";
    case proto::ELEMENT_TYPE_XMLHTTPREQUEST:
      return "xmlhttprequest";
    case proto::ELEMENT_TYPE_WEBSOCKET:
      return "websocket";
    case proto::ELEMENT_TYPE_PING:
      return "ping";
    default:
      return "other";
  }
}

TaffyAdblockMatchResult Match(const TaffyAdblockEngine* engine,
                              const GURL& url,
                              std::string_view initiator_host,
                              const char* request_type,
                              bool third_party,
                              bool disable_generic_rules) {
  return taffy_adblock_engine_matches(
      engine, AdblockView(url.spec()), AdblockView(url.host()),
      AdblockView(initiator_host), AdblockView(request_type), third_party,
      AdblockView("GET"), disable_generic_rules);
}

}  // namespace

CompiledRuleset::CompiledRuleset() = default;
CompiledRuleset::CompiledRuleset(CompiledRuleset&&) = default;
CompiledRuleset& CompiledRuleset::operator=(CompiledRuleset&&) = default;
CompiledRuleset::~CompiledRuleset() = default;

CompiledRuleset CompileRuleset(std::string_view list_text) {
  CompiledRuleset compiled;
  std::string_view remaining = list_text;
  while (!remaining.empty()) {
    const size_t newline = remaining.find('\n');
    remaining = newline == std::string_view::npos
                    ? std::string_view()
                    : remaining.substr(newline + 1);
    ++compiled.counts.lines;
  }

  TaffyAdblockCompileStats stats{};
  TaffyAdblockEngine* engine = taffy_adblock_engine_create_from_list(
      reinterpret_cast<const uint8_t*>(list_text.data()), list_text.size(),
      &stats);
  if (!engine) {
    return compiled;
  }
  compiled.counts.rules_indexed = stats.rules_indexed;
  compiled.counts.cosmetic_skipped = 0;
  compiled.counts.unsupported_skipped = stats.parse_errors;

  size_t serialized_len = 0;
  uint8_t* serialized = taffy_adblock_engine_serialize(engine, &serialized_len);
  taffy_adblock_engine_destroy(engine);
  if (serialized && serialized_len > 0) {
    compiled.bytes.resize(serialized_len);
    // The C ABI is the one place this Rust-owned allocation becomes a
    // bounds-carrying C++ view. Keep the unsafe pointer/length construction at
    // that boundary; every operation after it is span-checked.
    const auto serialized_bytes =
        UNSAFE_BUFFERS(base::span(serialized, serialized_len));
    base::span(compiled.bytes).copy_from(serialized_bytes);
    compiled.checksum = ChecksumOf(compiled.bytes);
    taffy_adblock_bytes_free(serialized, serialized_len);
  }
  return compiled;
}

// static
std::unique_ptr<FilterRulesetMatcher> FilterRulesetMatcher::Create(
    std::vector<uint8_t> bytes,
    int checksum) {
  if (bytes.empty() || ChecksumOf(bytes) != checksum) {
    return nullptr;
  }
  TaffyAdblockEngine* engine =
      taffy_adblock_engine_create_from_serialized(bytes.data(), bytes.size());
  if (!engine) {
    return nullptr;
  }
  return std::unique_ptr<FilterRulesetMatcher>(
      new FilterRulesetMatcher(engine));
}

FilterRulesetMatcher::FilterRulesetMatcher(TaffyAdblockEngine* engine)
    : engine_(engine) {}

FilterRulesetMatcher::~FilterRulesetMatcher() {
  taffy_adblock_engine_destroy(engine_);
  engine_ = nullptr;
}

bool FilterRulesetMatcher::IsDocumentAllowlisted(
    const GURL& document_url,
    const url::Origin& parent_origin) const {
  const bool third_party =
      !parent_origin.opaque() && document_url.host() != parent_origin.host();
  const TaffyAdblockMatchResult result =
      Match(engine_, document_url, parent_origin.host(), "document",
            third_party, false);
  return result.has_exception;
}

bool FilterRulesetMatcher::IsGenericBlockDisabled(
    const GURL& document_url,
    const url::Origin& parent_origin) const {
  (void)parent_origin;
  return taffy_adblock_engine_generic_block(engine_,
                                            AdblockView(document_url.spec()));
}

bool FilterRulesetMatcher::ShouldBlockRequest(
    const GURL& request_url,
    const url::Origin& document_origin,
    proto::ElementType element_type,
    bool disable_generic_rules) const {
  const bool third_party = request_url.host() != document_origin.host();
  const TaffyAdblockMatchResult result =
      Match(engine_, request_url, document_origin.host(),
            RequestTypeFor(element_type), third_party, disable_generic_rules);
  return result.matched;
}

CosmeticResources FilterRulesetMatcher::UrlCosmeticResources(
    const GURL& document_url) const {
  ScopedAdblockString json(taffy_adblock_engine_url_cosmetic_resources(
      engine_, AdblockView(document_url.spec())));
  if (!json) {
    return CosmeticResources();
  }
  return ParseUrlCosmeticResourcesJson(json.get())
      .value_or(CosmeticResources());
}

std::vector<std::string> FilterRulesetMatcher::HiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) const {
  std::vector<const char*> class_ptrs;
  class_ptrs.reserve(classes.size());
  for (const std::string& value : classes) {
    class_ptrs.push_back(value.c_str());
  }
  std::vector<const char*> id_ptrs;
  id_ptrs.reserve(ids.size());
  for (const std::string& value : ids) {
    id_ptrs.push_back(value.c_str());
  }
  std::vector<const char*> exception_ptrs;
  exception_ptrs.reserve(exceptions.size());
  for (const std::string& value : exceptions) {
    exception_ptrs.push_back(value.c_str());
  }
  ScopedAdblockString json(taffy_adblock_engine_hidden_class_id_selectors(
      engine_, class_ptrs.empty() ? nullptr : class_ptrs.data(),
      class_ptrs.size(), id_ptrs.empty() ? nullptr : id_ptrs.data(),
      id_ptrs.size(), exception_ptrs.empty() ? nullptr : exception_ptrs.data(),
      exception_ptrs.size()));
  if (!json) {
    return {};
  }
  return ParseSelectorListJson(json.get()).value_or(std::vector<std::string>());
}

}  // namespace taffy::filtering
