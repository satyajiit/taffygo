// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/content_metadata.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace taffy::content_metadata {
namespace {

char AsciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

bool ContainsAsciiCaseInsensitive(std::string_view haystack,
                                  std::string_view needle) {
  if (needle.empty() || needle.size() > haystack.size()) {
    return false;
  }
  for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
    bool equal = true;
    for (size_t offset = 0; offset < needle.size(); ++offset) {
      if (AsciiLower(haystack[start + offset]) != AsciiLower(needle[offset])) {
        equal = false;
        break;
      }
    }
    if (equal) {
      return true;
    }
  }
  return false;
}

bool ContainsAny(std::string_view text, const auto& needles) {
  return std::ranges::any_of(needles, [text](std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
  });
}

bool LooksLikeEncodedBlob(std::string_view text, size_t minimum_chars) {
  if (minimum_chars == 0 || text.size() < minimum_chars) {
    return false;
  }
  if (ContainsAsciiCaseInsensitive(text, ";base64,")) {
    return true;
  }

  size_t length = 0;
  bool lower = false;
  bool upper = false;
  bool digit = false;
  bool encoding_symbol = false;
  const auto reset = [&]() {
    length = 0;
    lower = false;
    upper = false;
    digit = false;
    encoding_symbol = false;
  };
  for (char value : text) {
    const bool is_lower = value >= 'a' && value <= 'z';
    const bool is_upper = value >= 'A' && value <= 'Z';
    const bool is_digit = value >= '0' && value <= '9';
    const bool is_symbol = value == '+' || value == '/' || value == '=' ||
                           value == '_' || value == '-';
    if (!is_lower && !is_upper && !is_digit && !is_symbol) {
      reset();
      continue;
    }
    ++length;
    lower |= is_lower;
    upper |= is_upper;
    digit |= is_digit;
    // A hyphen alone is common in prose and identifiers. Count it as encoding
    // punctuation only with the other base64/base64url evidence.
    encoding_symbol |=
        value == '+' || value == '/' || value == '=' || value == '_';
    if (length >= minimum_chars && digit && encoding_symbol &&
        (lower || upper)) {
      return true;
    }
  }
  return false;
}

size_t PrimaryLanguageLength(std::string_view language) {
  size_t length = 0;
  while (length < language.size() && language[length] != '-' &&
         language[length] != '_') {
    ++length;
  }
  return length;
}

int TrustRank(RendererContentTrust trust) {
  switch (trust) {
    case RendererContentTrust::kFirstPartyDocument:
      return 0;
    case RendererContentTrust::kUserGeneratedContent:
      return 1;
    case RendererContentTrust::kThirdPartyEmbedded:
      return 2;
    case RendererContentTrust::kUnknownUntrusted:
      return 3;
  }
}

}  // namespace

RendererContentTrust DocumentTrust(bool browser_reported_cross_origin) {
  return browser_reported_cross_origin
             ? RendererContentTrust::kThirdPartyEmbedded
             : RendererContentTrust::kFirstPartyDocument;
}

RendererContentTrust UserGeneratedTrust(bool browser_reported_cross_origin) {
  return browser_reported_cross_origin
             ? RendererContentTrust::kThirdPartyEmbedded
             : RendererContentTrust::kUserGeneratedContent;
}

RendererContentTrust LeastTrusted(RendererContentTrust left,
                                  RendererContentTrust right) {
  return TrustRank(left) >= TrustRank(right) ? left : right;
}

ContentSignalMask ContextSignals(bool hidden_by_style,
                                 bool language_mismatch,
                                 bool browser_reported_cross_origin) {
  ContentSignalMask signals = 0;
  if (hidden_by_style) {
    signals |= ContentSignalBit(ContentSignal::kHiddenByStyle);
  }
  if (language_mismatch) {
    signals |= ContentSignalBit(ContentSignal::kLanguageMismatch);
  }
  if (browser_reported_cross_origin) {
    signals |= ContentSignalBit(ContentSignal::kCrossOriginFrameAuthored);
  }
  return signals;
}

bool AuthoredLanguagesDiffer(std::string_view document_language,
                             std::string_view content_language) {
  const size_t document_length = PrimaryLanguageLength(document_language);
  const size_t content_length = PrimaryLanguageLength(content_language);
  if (document_length == 0 || content_length == 0 ||
      document_length != content_length) {
    return document_length != 0 && content_length != 0;
  }
  for (size_t index = 0; index < document_length; ++index) {
    if (AsciiLower(document_language[index]) !=
        AsciiLower(content_language[index])) {
      return true;
    }
  }
  return false;
}

ContentSignalMask DetectTextSignals(std::string_view text,
                                    const ContentSignalLimits& limits) {
  text = text.substr(
      0, std::min<size_t>(text.size(), limits.max_text_scan_bytes()));
  if (text.empty()) {
    return 0;
  }

  ContentSignalMask signals = 0;
  constexpr auto kZeroWidthUtf8 = std::to_array<std::string_view>({
      "\xE2\x80\x8B",  // ZERO WIDTH SPACE
      "\xE2\x80\x8C",  // ZERO WIDTH NON-JOINER
      "\xE2\x80\x8D",  // ZERO WIDTH JOINER
      "\xE2\x81\xA0",  // WORD JOINER
      "\xEF\xBB\xBF",  // ZERO WIDTH NO-BREAK SPACE
  });
  if (ContainsAny(text, kZeroWidthUtf8)) {
    signals |= ContentSignalBit(ContentSignal::kZeroWidthCharacters);
  }

  constexpr auto kBidiControlUtf8 = std::to_array<std::string_view>({
      "\xD8\x9C",      // ARABIC LETTER MARK
      "\xE2\x80\x8E",  // LEFT-TO-RIGHT MARK
      "\xE2\x80\x8F",  // RIGHT-TO-LEFT MARK
      "\xE2\x80\xAA",  // LEFT-TO-RIGHT EMBEDDING
      "\xE2\x80\xAB",  // RIGHT-TO-LEFT EMBEDDING
      "\xE2\x80\xAC",  // POP DIRECTIONAL FORMATTING
      "\xE2\x80\xAD",  // LEFT-TO-RIGHT OVERRIDE
      "\xE2\x80\xAE",  // RIGHT-TO-LEFT OVERRIDE
      "\xE2\x81\xA6",  // LEFT-TO-RIGHT ISOLATE
      "\xE2\x81\xA7",  // RIGHT-TO-LEFT ISOLATE
      "\xE2\x81\xA8",  // FIRST STRONG ISOLATE
      "\xE2\x81\xA9",  // POP DIRECTIONAL ISOLATE
  });
  if (ContainsAny(text, kBidiControlUtf8)) {
    signals |= ContentSignalBit(ContentSignal::kBidiControlCharacters);
  }

  if (LooksLikeEncodedBlob(text, limits.min_encoded_blob_chars())) {
    signals |= ContentSignalBit(ContentSignal::kEncodedBlob);
  }

  constexpr auto kImperativeShapes = std::to_array<std::string_view>({
      "ignore previous instruction",
      "ignore all previous",
      "disregard previous instruction",
      "follow these instruction",
      "reveal your prompt",
      "system prompt",
      "developer message",
      "do not tell the user",
      "call the tool",
  });
  if (std::ranges::any_of(kImperativeShapes, [text](std::string_view shape) {
        return ContainsAsciiCaseInsensitive(text, shape);
      })) {
    signals |= ContentSignalBit(ContentSignal::kImperativeInstructionShape);
  }
  return signals;
}

void ApplyNodeContext(SemanticNode* node,
                      RendererContentTrust trust,
                      ContentSignalMask context_signals) {
  node->content_trust = trust;
  node->content_signals |= context_signals;
}

void AddNodeTextSignals(SemanticNode* node,
                        std::string_view text,
                        ContentSignalMask context_signals,
                        const ContentSignalLimits& limits) {
  node->content_signals |= context_signals | DetectTextSignals(text, limits);
}

void LabelTextRun(TextRun* run,
                  SemanticNode* node,
                  RendererContentTrust trust,
                  ContentSignalMask context_signals,
                  const ContentSignalLimits& limits) {
  run->content_trust = trust;
  run->content_signals |=
      context_signals | DetectTextSignals(run->text, limits);
  node->content_signals |= run->content_signals;
}

}  // namespace taffy::content_metadata
