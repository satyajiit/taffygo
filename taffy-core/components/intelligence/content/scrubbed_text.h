// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBED_TEXT_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBED_TEXT_H_

#include <stdint.h>

#include <string>
#include <type_traits>

// Text that has passed the scrubber, and a type that cannot be forged
// (PAR-SEC-009: zero seeded-secret hits across browser, AI runtime, analytics
// and crash paths; REQ-DATA-003).
//
// The usual shape of this rule is "remember to redact before logging", and the
// usual outcome is that one call site out of forty does not. This type turns
// the rule into a signature: every logging, crash-key and analytics entry
// point in //taffy takes a ScrubbedText, and the only way to obtain
// one is ScrubbingSerializer, which is its sole friend. There is no public
// constructor, no conversion from std::string, and no assignment from one.
// Passing an unscrubbed string where a scrubbed one is required is not a
// review finding; it does not compile.
//
// The report travels with the text on purpose. A caller that wants to know
// whether anything was removed — the seeded-secret suite, most of all — reads
// it from the same object rather than re-scanning, and a canary that reached
// the scrubber is counted rather than silently cleaned. A canary reaching this
// point means an upstream redaction failed, and a count is how that failure
// becomes visible instead of invisible.

namespace taffy {

// Which rules fired. A bit field so the report stays a plain value.
enum class ScrubRule : uint32_t {
  kNone = 0,
  // A URL was reduced to its origin. Full URLs and query strings are forbidden
  // in analytics (protocol section 16) and are the most common way a token
  // reaches a log.
  kUrlReducedToOrigin = 1 << 0,
  // Credentials embedded in a URL's authority.
  kUrlUserInfo = 1 << 1,
  // A parameter or field whose name says the value is sensitive.
  kSensitiveParameterValue = 1 << 2,
  kBearerToken = 1 << 3,
  kJsonWebToken = 1 << 4,
  kPemBlock = 1 << 5,
  // A long mixed-case run from the base64 or hexadecimal alphabet. The
  // catch-all for keys and tokens that carry no label.
  kHighEntropyRun = 1 << 6,
  // A digit run that passes the Luhn check.
  kPaymentCardNumber = 1 << 7,
  // A seeded canary from the fixture corpus reached the scrubber. Always a
  // defect upstream of here, never a normal event.
  kCanaryTripwire = 1 << 8,
};

constexpr uint32_t AsMask(ScrubRule rule) {
  return static_cast<uint32_t>(rule);
}

constexpr bool RuleFired(uint32_t mask, ScrubRule rule) {
  return (mask & AsMask(rule)) != 0;
}

struct ScrubReport {
  uint32_t applied_rule_mask = AsMask(ScrubRule::kNone);
  // How many separate spans were replaced.
  uint32_t redaction_count = 0;
  // How many seeded fixture canaries were present in the *input*. Counted
  // from the input rather than from the redactions, so that a canary another
  // rule happened to cover — inside a URL, a certificate block or a bearer
  // token — is still reported. Non-zero is always a defect upstream of the
  // scrubber.
  uint32_t canary_hit_count = 0;

  bool anything_was_removed() const { return redaction_count > 0; }

  friend bool operator==(const ScrubReport&, const ScrubReport&) = default;
};

static_assert(std::is_trivially_copyable_v<ScrubReport>,
              "The scrub report carries counts and a bit mask only. An owning "
              "member would create a place for the removed text to survive, "
              "which is the opposite of the point.");

class ScrubbedText {
 public:
  ScrubbedText(const ScrubbedText&);
  ScrubbedText& operator=(const ScrubbedText&);
  ScrubbedText(ScrubbedText&&);
  ScrubbedText& operator=(ScrubbedText&&);
  ~ScrubbedText();

  const std::string& value() const { return value_; }
  const ScrubReport& report() const { return report_; }

 private:
  // The sole producer. There is no other way to make one of these, which is
  // the entire design.
  friend class ScrubbingSerializer;
  ScrubbedText(std::string value, ScrubReport report);

  std::string value_;
  ScrubReport report_;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBED_TEXT_H_
