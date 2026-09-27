// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_CORE_RULESET_H_
#define TAFFY_COMPONENTS_FILTERING_CORE_RULESET_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr_exclusion.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "taffy/components/filtering/core/cosmetic_resources.h"

class GURL;

namespace url {
class Origin;
}

struct TaffyAdblockEngine;

namespace taffy::filtering {

// What one compile run made of one body of filter-list text.
struct RulesetCounts {
  uint64_t lines = 0;
  uint64_t rules_indexed = 0;
  uint64_t cosmetic_skipped = 0;
  uint64_t unsupported_skipped = 0;
};

// The serialized adblock-rust engine, plus the checksum a loader must present
// the bytes back with. Decision 0086: matching is Brave's engine through a C
// ABI, not Chromium's url-pattern index.
struct CompiledRuleset {
  CompiledRuleset();
  CompiledRuleset(CompiledRuleset&&);
  CompiledRuleset& operator=(CompiledRuleset&&);
  ~CompiledRuleset();

  std::vector<uint8_t> bytes;
  int checksum = 0;
  RulesetCounts counts;
};

// Compiles EasyList-syntax text — one list or several concatenated — into the
// engine's serialized form. Pure and CPU-bound: callers own the thread it
// runs on.
CompiledRuleset CompileRuleset(std::string_view list_text);

// One loaded, verified ruleset answering requests. Immutable after creation,
// so a reference may be shared across threads.
class FilterRulesetMatcher {
 public:
  static std::unique_ptr<FilterRulesetMatcher> Create(
      std::vector<uint8_t> bytes,
      int checksum);

  ~FilterRulesetMatcher();
  FilterRulesetMatcher(const FilterRulesetMatcher&) = delete;
  FilterRulesetMatcher& operator=(const FilterRulesetMatcher&) = delete;

  bool IsDocumentAllowlisted(const GURL& document_url,
                             const url::Origin& parent_origin) const;

  bool IsGenericBlockDisabled(const GURL& document_url,
                              const url::Origin& parent_origin) const;

  bool ShouldBlockRequest(const GURL& request_url,
                          const url::Origin& document_origin,
                          url_pattern_index::proto::ElementType element_type,
                          bool disable_generic_rules) const;

  CosmeticResources UrlCosmeticResources(const GURL& document_url) const;

  std::vector<std::string> HiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions) const;

 private:
  explicit FilterRulesetMatcher(TaffyAdblockEngine* engine);

  // C ABI handle; destroyed by taffy_adblock_engine_destroy, not by Chromium.
  RAW_PTR_EXCLUSION TaffyAdblockEngine* engine_ = nullptr;
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_CORE_RULESET_H_
