// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/rule_parser.h"

#include <string>
#include <vector>

namespace taffy::filtering {

namespace proto = url_pattern_index::proto;

namespace {

// The element types a rule with no type option applies to. ABP semantics:
// every resource class except pop-ups, which must be asked for by name.
constexpr int kDefaultElementTypes =
    proto::ELEMENT_TYPE_ALL & ~proto::ELEMENT_TYPE_POPUP;

std::string_view Trimmed(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                           text.front() == '\r')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

bool IsCosmetic(std::string_view line) {
  // `##`, `#@#`, `#?#`, `#$#` and `#%#` all mark element-hiding or snippet
  // rules. A lone `#` inside a URL pattern is legal, so the marker needs the
  // doubled shape.
  for (size_t index = 0; index + 1 < line.size(); ++index) {
    if (line[index] != '#') {
      continue;
    }
    const char next = line[index + 1];
    if (next == '#') {
      return true;
    }
    if ((next == '@' || next == '?' || next == '$' || next == '%') &&
        index + 2 < line.size() && line[index + 2] == '#') {
      return true;
    }
  }
  return false;
}

std::optional<proto::ElementType> NamedElementType(std::string_view name) {
  if (name == "script") {
    return proto::ELEMENT_TYPE_SCRIPT;
  }
  if (name == "image") {
    return proto::ELEMENT_TYPE_IMAGE;
  }
  if (name == "stylesheet") {
    return proto::ELEMENT_TYPE_STYLESHEET;
  }
  if (name == "object") {
    return proto::ELEMENT_TYPE_OBJECT;
  }
  if (name == "xmlhttprequest") {
    return proto::ELEMENT_TYPE_XMLHTTPREQUEST;
  }
  if (name == "object-subrequest") {
    return proto::ELEMENT_TYPE_OBJECT_SUBREQUEST;
  }
  if (name == "subdocument") {
    return proto::ELEMENT_TYPE_SUBDOCUMENT;
  }
  if (name == "ping") {
    return proto::ELEMENT_TYPE_PING;
  }
  if (name == "media") {
    return proto::ELEMENT_TYPE_MEDIA;
  }
  if (name == "font") {
    return proto::ELEMENT_TYPE_FONT;
  }
  if (name == "popup") {
    return proto::ELEMENT_TYPE_POPUP;
  }
  if (name == "websocket") {
    return proto::ELEMENT_TYPE_WEBSOCKET;
  }
  if (name == "other") {
    return proto::ELEMENT_TYPE_OTHER;
  }
  return std::nullopt;
}

// Applies one `$`-option to the rule under construction. False means the
// option is one this parser does not express, which skips the whole rule.
bool ApplyOption(std::string_view option,
                 bool is_allow_rule,
                 proto::UrlRule& rule,
                 int& included_types,
                 int& excluded_types,
                 int& activation_types) {
  const bool inverted = !option.empty() && option.front() == '~';
  if (inverted) {
    option.remove_prefix(1);
  }
  if (option.rfind("domain=", 0) == 0 && !inverted) {
    std::string_view list = option.substr(7);
    while (!list.empty()) {
      const size_t bar = list.find('|');
      std::string_view entry = list.substr(0, bar);
      list = bar == std::string_view::npos ? std::string_view()
                                          : list.substr(bar + 1);
      const bool exclude = !entry.empty() && entry.front() == '~';
      if (exclude) {
        entry.remove_prefix(1);
      }
      if (entry.empty()) {
        return false;
      }
      auto* item = rule.add_initiator_domains();
      item->set_domain(std::string(entry));
      item->set_exclude(exclude);
    }
    return true;
  }
  if (option == "third-party") {
    rule.set_source_type(inverted ? proto::SOURCE_TYPE_FIRST_PARTY
                                  : proto::SOURCE_TYPE_THIRD_PARTY);
    return true;
  }
  if (option == "match-case" && !inverted) {
    rule.set_match_case(true);
    return true;
  }
  if (option == "document") {
    // Allowing a whole document is an activation this engine expresses;
    // blocking one is `$document` on a blocking rule, which it does not.
    if (!is_allow_rule || inverted) {
      return false;
    }
    activation_types |= proto::ACTIVATION_TYPE_DOCUMENT;
    return true;
  }
  if (option == "genericblock") {
    if (!is_allow_rule || inverted) {
      return false;
    }
    activation_types |= proto::ACTIVATION_TYPE_GENERICBLOCK;
    return true;
  }
  if (const auto element = NamedElementType(option)) {
    if (inverted) {
      excluded_types |= *element;
    } else {
      included_types |= *element;
    }
    return true;
  }
  // `elemhide`, `generichide`, `csp=`, `redirect=`, `removeparam`, `header=`,
  // `important`, `badfilter`, `all`, and anything newer: not expressed here.
  return false;
}

}  // namespace

ParsedLine ParseRuleLine(std::string_view raw) {
  std::string_view line = Trimmed(raw);
  if (line.empty() || line.front() == '!' || line.front() == '[') {
    return {LineKind::kComment, std::nullopt};
  }
  if (IsCosmetic(line)) {
    return {LineKind::kCosmetic, std::nullopt};
  }

  proto::UrlRule rule;
  rule.set_semantics(proto::RULE_SEMANTICS_BLOCKLIST);
  bool is_allow = false;
  if (line.rfind("@@", 0) == 0) {
    is_allow = true;
    rule.set_semantics(proto::RULE_SEMANTICS_ALLOWLIST);
    line.remove_prefix(2);
  }

  // Options follow the last `$`. A pattern with no options has no `$` in
  // EasyList practice; a regular-expression rule is refused before this
  // split could misread one.
  std::string_view pattern = line;
  std::string_view options;
  const size_t dollar = line.rfind('$');
  if (dollar != std::string_view::npos) {
    pattern = line.substr(0, dollar);
    options = line.substr(dollar + 1);
  }
  pattern = Trimmed(pattern);
  if (pattern.size() >= 2 && pattern.front() == '/' && pattern.back() == '/') {
    return {LineKind::kUnsupported, std::nullopt};
  }

  int included_types = 0;
  int excluded_types = 0;
  int activation_types = 0;
  while (!options.empty()) {
    const size_t comma = options.find(',');
    std::string_view option = Trimmed(options.substr(0, comma));
    options = comma == std::string_view::npos ? std::string_view()
                                              : options.substr(comma + 1);
    if (option.empty()) {
      continue;
    }
    if (!ApplyOption(option, is_allow, rule, included_types, excluded_types,
                     activation_types)) {
      return {LineKind::kUnsupported, std::nullopt};
    }
  }

  if (pattern.rfind("||", 0) == 0) {
    rule.set_anchor_left(proto::ANCHOR_TYPE_SUBDOMAIN);
    pattern.remove_prefix(2);
  } else if (!pattern.empty() && pattern.front() == '|') {
    rule.set_anchor_left(proto::ANCHOR_TYPE_BOUNDARY);
    pattern.remove_prefix(1);
  }
  if (!pattern.empty() && pattern.back() == '|') {
    rule.set_anchor_right(proto::ANCHOR_TYPE_BOUNDARY);
    pattern.remove_suffix(1);
  }
  rule.set_url_pattern_type(pattern.find('*') != std::string_view::npos
                                ? proto::URL_PATTERN_TYPE_WILDCARDED
                                : proto::URL_PATTERN_TYPE_SUBSTRING);
  rule.set_url_pattern(std::string(pattern));

  // A pattern that constrains nothing is a rule about everything. Activation
  // rules legitimately take that shape (`@@$document,domain=...`); a network
  // rule may only when a domain list scopes it.
  const bool unconstrained = pattern.empty() || pattern == "*";
  if (unconstrained && rule.initiator_domains().empty() &&
      activation_types == 0) {
    return {LineKind::kUnsupported, std::nullopt};
  }

  if (activation_types != 0) {
    rule.set_activation_types(activation_types);
    // A pure activation rule matches no element; one that also names types
    // keeps them.
    if (included_types == 0 && excluded_types == 0) {
      rule.set_element_types(0);
      return {LineKind::kRule, std::move(rule)};
    }
  }
  int element_types = included_types != 0 ? included_types
                                          : kDefaultElementTypes;
  element_types &= ~excluded_types;
  if (element_types == 0) {
    return {LineKind::kUnsupported, std::nullopt};
  }
  rule.set_element_types(element_types);
  return {LineKind::kRule, std::move(rule)};
}

}  // namespace taffy::filtering
