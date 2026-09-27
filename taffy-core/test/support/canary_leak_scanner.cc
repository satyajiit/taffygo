// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/canary_leak_scanner.h"

#include <array>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/recording_audit_stream.h"
#include "taffy/test/support/recording_journal.h"

namespace taffy::test {

namespace {

// Percent-encodes every character that is not unreserved. A value that reached
// a sink through a URL, a query string or a form encoding arrives in this
// shape, and a redactor that only suppressed the plain bytes would miss it.
std::string PercentEncode(const std::string &value) {
  std::string out;
  out.reserve(value.size() * 3);
  for (const char character : value) {
    const bool unreserved = base::IsAsciiAlphaNumeric(character) ||
                            character == '-' || character == '.' ||
                            character == '_' || character == '~';
    if (unreserved) {
      out.push_back(character);
      continue;
    }
    // Written by hand rather than through a helper: the two-character
    // upper-case form is what a URL encoder emits, and this file should not
    // acquire a dependency whose spelling has to be re-verified at every
    // milestone for four lines of hexadecimal.
    // std::array rather than a C array: indexing a C array is an unsafe
    // buffer access under -Wunsafe-buffer-usage, and both indices below are
    // provably in range (a byte's two nibbles are each 0-15) so the right
    // answer is the container that says so rather than a suppression.
    static constexpr std::array<char, 16> kHexDigits = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    const unsigned char byte = static_cast<unsigned char>(character);
    out.push_back('%');
    out.push_back(kHexDigits[byte >> 4]);
    out.push_back(kHexDigits[byte & 0x0F]);
  }
  return out;
}

std::string StripSeparators(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (const char character : value) {
    if (character != '-' && character != ' ' && character != '_') {
      out.push_back(character);
    }
  }
  return out;
}

std::string IdentifierText(const RecordIdentifier &identifier) {
  return std::string(identifier.chars.data());
}

} // namespace

// static
std::vector<std::pair<std::string, std::string>>
CanaryLeakScanner::VariantsOf(const std::string &value) {
  std::vector<std::pair<std::string, std::string>> variants;
  variants.emplace_back(value, "verbatim");
  // A sink that lower-cased or upper-cased on the way — a log formatter, a
  // header name normalizer — still leaked the value.
  const std::string lower = base::ToLowerASCII(value);
  if (lower != value) {
    variants.emplace_back(lower, "lower-cased");
  }
  const std::string upper = base::ToUpperASCII(value);
  if (upper != value) {
    variants.emplace_back(upper, "upper-cased");
  }
  // Reached a sink through a URL or a form encoding.
  const std::string encoded = PercentEncode(value);
  if (encoded != value) {
    variants.emplace_back(encoded, "percent-encoded");
  }
  // Reached a sink after a formatter removed the separators, which is what a
  // card-number or recovery-code normalizer does.
  const std::string stripped = StripSeparators(value);
  if (stripped != value) {
    variants.emplace_back(stripped, "separators removed");
  }
  // Reached a sink inside an encoded blob. This is the variant that catches a
  // payload wrapped for transport rather than a formatting accident.
  // VERIFY AT SP-01: base::Base64Encode's string_view overload returning
  // std::string. Upstream file to read: base/base64.h. If only the span form
  // survives, this call takes base::as_byte_span(value) and nothing else here
  // changes.
  variants.emplace_back(base::Base64Encode(value), "base64-encoded");
  return variants;
}

// static
CanaryLeakScanner CanaryLeakScanner::ForWholeCorpus() {
  return CanaryLeakScanner(CorpusManifest::Get().AllCanaryTokens());
}

// static
CanaryLeakScanner CanaryLeakScanner::ForFixture(std::string_view fixture_id) {
  const CorpusManifest &manifest = CorpusManifest::Get();
  std::vector<std::string> values = manifest.AllCanaryTokens();
  for (const std::string &omission : manifest.ProhibitedValuesFor(fixture_id)) {
    values.push_back(omission);
  }
  return CanaryLeakScanner(std::move(values));
}

CanaryLeakScanner::CanaryLeakScanner(std::vector<std::string> prohibited_values)
    : prohibited_values_(std::move(prohibited_values)) {
  CHECK(!prohibited_values_.empty())
      << "A leak scanner with nothing to look for passes for the wrong reason. "
         "The corpus declares its canaries; if this list is empty the corpus "
         "is "
         "not mounted or its manifest lost them.";
  // A short prohibited value would match almost any text and turn the suite
  // into a false-positive generator. The corpus's tokens are long and
  // distinctive on purpose, so this is a corpus defect rather than a limit.
  for (const std::string &value : prohibited_values_) {
    CHECK_GE(value.size(), 8u)
        << "Prohibited value " << value
        << " is too short to search for without matching ordinary text. A "
           "seeded secret has to be distinctive to be measurable.";
  }
}

CanaryLeakScanner::CanaryLeakScanner(CanaryLeakScanner &&) noexcept = default;
CanaryLeakScanner::~CanaryLeakScanner() = default;

void CanaryLeakScanner::AddSink(std::string name, std::string content) {
  sinks_.push_back(Sink{std::move(name), std::move(content)});
}

void CanaryLeakScanner::AddJournal(const RecordingJournal &journal) {
  std::string content;
  for (const DispatchIntentRecord &record : journal.intents()) {
    content = base::StrCat({content, record.dispatch_id.value,
                            " ",     record.task_id.value,
                            " ",     record.action_id.value,
                            " ",     record.capability_reference.value,
                            " ",     record.actor_lease_id.value,
                            " ",     record.tab_id.value,
                            " ",     record.frame_id.value,
                            " ",     record.page_epoch.value,
                            " ",     record.origin.serialization,
                            " ",     record.origin.opaque_id,
                            "\n"});
  }
  for (const ActionResult &result : journal.terminal_results()) {
    content =
        base::StrCat({content, result.request_id.value, " ",
                      result.action_id.value, " ", result.dispatch_id.value,
                      " ", result.detail_code.value_or(std::string()), "\n"});
  }
  AddSink("task journal", std::move(content));
}

void CanaryLeakScanner::AddAuditStream(const RecordingAuditStream &stream) {
  std::string content;
  for (const ObservationRecord &record : stream.observations()) {
    content = base::StrCat({content, IdentifierText(record.request_id), " ",
                            IdentifierText(record.authority_subject_id), " ",
                            IdentifierText(record.tab_id), " ",
                            IdentifierText(record.frame_id), " ",
                            IdentifierText(record.page_epoch), "\n"});
  }
  for (const ActionRecord &record : stream.actions()) {
    content = base::StrCat({content, IdentifierText(record.request_id), " ",
                            IdentifierText(record.dispatch_id), " ",
                            IdentifierText(record.task_id), " ",
                            IdentifierText(record.action_id), " ",
                            IdentifierText(record.tab_id), " ",
                            IdentifierText(record.frame_id), " ",
                            IdentifierText(record.page_epoch), " ",
                            IdentifierText(record.capability_reference), " ",
                            IdentifierText(record.actor_lease_id), "\n"});
  }
  for (const SubscriptionRecord &record : stream.subscriptions()) {
    content = base::StrCat({content, IdentifierText(record.request_id), " ",
                            IdentifierText(record.subscription_id), " ",
                            IdentifierText(record.task_id), " ",
                            IdentifierText(record.tab_id), " ",
                            IdentifierText(record.frame_id), " ",
                            IdentifierText(record.page_epoch), "\n"});
  }
  AddSink("audit stream", std::move(content));
}

std::vector<std::string> CanaryLeakScanner::Scan(std::string_view text) const {
  std::vector<std::string> found;
  for (const std::string &value : prohibited_values_) {
    for (const auto &[spelling, reason] : VariantsOf(value)) {
      if (text.find(spelling) != std::string_view::npos) {
        found.push_back(base::StrCat({value, " (", reason, ")"}));
        break;
      }
    }
  }
  return found;
}

::testing::AssertionResult CanaryLeakScanner::AssertAllSinksClean() const {
  CHECK(!sinks_.empty())
      << "AssertAllSinksClean() with no sink registered asserts nothing. "
         "Register every sink the test can reach; the user interface hiding a "
         "value is not evidence that the value stayed on the device.";
  std::string report;
  size_t finding_count = 0;
  for (const Sink &sink : sinks_) {
    for (const std::string &finding : Scan(sink.content)) {
      ++finding_count;
      report = base::StrCat({report, "\n  ", sink.name, " carries ", finding});
    }
  }
  if (finding_count == 0) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "A seeded secret reached " << finding_count
         << " egress sink position. This is a zero-tolerance invariant, not a "
            "rate to improve:"
         << report;
}

} // namespace taffy::test
