// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/core/rule_parser.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

namespace proto = url_pattern_index::proto;

TEST(FilterRuleParserTest, CommentsHeadersAndBlanksAreComments) {
  EXPECT_EQ(LineKind::kComment, ParseRuleLine("").kind);
  EXPECT_EQ(LineKind::kComment, ParseRuleLine("   \r").kind);
  EXPECT_EQ(LineKind::kComment, ParseRuleLine("! EasyList").kind);
  EXPECT_EQ(LineKind::kComment, ParseRuleLine("[Adblock Plus 2.0]").kind);
}

TEST(FilterRuleParserTest, CosmeticRulesAreCountedNeverGuessedAt) {
  EXPECT_EQ(LineKind::kCosmetic, ParseRuleLine("example.test##.ad").kind);
  EXPECT_EQ(LineKind::kCosmetic, ParseRuleLine("example.test#@#.ad").kind);
  EXPECT_EQ(LineKind::kCosmetic, ParseRuleLine("example.test#?#.ad").kind);
  EXPECT_EQ(LineKind::kCosmetic,
            ParseRuleLine("example.test#$#body { margin: 0 }").kind);
}

TEST(FilterRuleParserTest, ADomainAnchoredBlockRuleParsesWhole) {
  const ParsedLine parsed = ParseRuleLine("||ads.example^");
  ASSERT_EQ(LineKind::kRule, parsed.kind);
  EXPECT_EQ(proto::RULE_SEMANTICS_BLOCKLIST, parsed.rule->semantics());
  EXPECT_EQ(proto::ANCHOR_TYPE_SUBDOMAIN, parsed.rule->anchor_left());
  EXPECT_EQ(proto::URL_PATTERN_TYPE_SUBSTRING,
            parsed.rule->url_pattern_type());
  EXPECT_EQ("ads.example^", parsed.rule->url_pattern());
  // No type option: every default element class, pop-ups excluded.
  EXPECT_NE(0, parsed.rule->element_types() & proto::ELEMENT_TYPE_SCRIPT);
  EXPECT_EQ(0, parsed.rule->element_types() & proto::ELEMENT_TYPE_POPUP);
}

TEST(FilterRuleParserTest, AnExceptionRuleIsAllowlistSemantics) {
  const ParsedLine parsed = ParseRuleLine("@@||cdn.example^$script");
  ASSERT_EQ(LineKind::kRule, parsed.kind);
  EXPECT_EQ(proto::RULE_SEMANTICS_ALLOWLIST, parsed.rule->semantics());
  EXPECT_EQ(proto::ELEMENT_TYPE_SCRIPT, parsed.rule->element_types());
}

TEST(FilterRuleParserTest, OptionsScopeTheRule) {
  const ParsedLine parsed = ParseRuleLine(
      "||track.example^$third-party,image,domain=news.example|~sub.news.example");
  ASSERT_EQ(LineKind::kRule, parsed.kind);
  EXPECT_EQ(proto::SOURCE_TYPE_THIRD_PARTY, parsed.rule->source_type());
  EXPECT_EQ(proto::ELEMENT_TYPE_IMAGE, parsed.rule->element_types());
  ASSERT_EQ(2, parsed.rule->initiator_domains_size());
  EXPECT_EQ("news.example", parsed.rule->initiator_domains(0).domain());
  EXPECT_FALSE(parsed.rule->initiator_domains(0).exclude());
  EXPECT_EQ("sub.news.example", parsed.rule->initiator_domains(1).domain());
  EXPECT_TRUE(parsed.rule->initiator_domains(1).exclude());
}

TEST(FilterRuleParserTest, InvertedTypesSubtractFromTheDefaults) {
  const ParsedLine parsed = ParseRuleLine("||wide.example^$~image");
  ASSERT_EQ(LineKind::kRule, parsed.kind);
  EXPECT_EQ(0, parsed.rule->element_types() & proto::ELEMENT_TYPE_IMAGE);
  EXPECT_NE(0, parsed.rule->element_types() & proto::ELEMENT_TYPE_SCRIPT);
}

TEST(FilterRuleParserTest, DocumentActivationRidesOnlyAnExceptionRule) {
  const ParsedLine allow = ParseRuleLine("@@||trusted.example^$document");
  ASSERT_EQ(LineKind::kRule, allow.kind);
  EXPECT_EQ(proto::ACTIVATION_TYPE_DOCUMENT, allow.rule->activation_types());
  EXPECT_EQ(0, allow.rule->element_types());

  EXPECT_EQ(LineKind::kUnsupported,
            ParseRuleLine("||locked.example^$document").kind);
}

TEST(FilterRuleParserTest, WhatThisEngineCannotExpressIsRefusedWhole) {
  EXPECT_EQ(LineKind::kUnsupported, ParseRuleLine("/banner[0-9]+/").kind);
  EXPECT_EQ(LineKind::kUnsupported,
            ParseRuleLine("||cdn.example^$csp=script-src 'none'").kind);
  EXPECT_EQ(LineKind::kUnsupported,
            ParseRuleLine("||cdn.example^$redirect=noopjs").kind);
  EXPECT_EQ(LineKind::kUnsupported,
            ParseRuleLine("@@||site.example^$elemhide").kind);
  // A rule about everything, scoped by nothing.
  EXPECT_EQ(LineKind::kUnsupported, ParseRuleLine("*").kind);
}

TEST(FilterRuleParserTest, AnchorsAndWildcardsAreCarried) {
  const ParsedLine parsed = ParseRuleLine("|https://exact.example/path|");
  ASSERT_EQ(LineKind::kRule, parsed.kind);
  EXPECT_EQ(proto::ANCHOR_TYPE_BOUNDARY, parsed.rule->anchor_left());
  EXPECT_EQ(proto::ANCHOR_TYPE_BOUNDARY, parsed.rule->anchor_right());

  const ParsedLine wildcard = ParseRuleLine("/ads/*/banner.");
  ASSERT_EQ(LineKind::kRule, wildcard.kind);
  EXPECT_EQ(proto::URL_PATTERN_TYPE_WILDCARDED,
            wildcard.rule->url_pattern_type());
}

}  // namespace
}  // namespace taffy::filtering
