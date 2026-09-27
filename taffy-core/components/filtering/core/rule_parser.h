// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_CORE_RULE_PARSER_H_
#define TAFFY_COMPONENTS_FILTERING_CORE_RULE_PARSER_H_

#include <optional>
#include <string_view>

#include "components/url_pattern_index/proto/rules.pb.h"

namespace taffy::filtering {

// What one filter-list line turned out to be. The parser is a total function
// over lines: every line is classified, and a rule this build cannot express
// is counted rather than silently dropped, so the compile report says what
// fraction of a list is actually standing.
enum class LineKind {
  // Blank, a comment (`!`), or a list header (`[Adblock ...]`).
  kComment,
  // An element-hiding or snippet rule (`##`, `#@#`, `#?#`, `#$#`, `#%#`).
  // The matching engine indexes these; this classifier still names the line
  // so a compile report can say what a list contains.
  kCosmetic,
  // A network rule carrying an option or shape this parser does not express
  // (a regular-expression pattern, `csp=`, `redirect=`, `removeparam`, ...).
  // Skipped whole: half-applying a rule would enforce something its author
  // did not write.
  kUnsupported,
  // A network rule, carried in `rule`.
  kRule,
};

struct ParsedLine {
  LineKind kind = LineKind::kComment;
  // Present exactly when `kind == kRule`.
  std::optional<url_pattern_index::proto::UrlRule> rule;
};

// Parses one line of EasyList-syntax filter text. Pure; never throws; a line
// it cannot express comes back classified, never approximated.
ParsedLine ParseRuleLine(std::string_view line);

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_CORE_RULE_PARSER_H_
