// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_CANARY_LEAK_SCANNER_H_
#define TAFFY_TEST_SUPPORT_CANARY_LEAK_SCANNER_H_

#include <string>
#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

class RecordingAuditStream;
class RecordingJournal;

// Searches every egress sink a test can reach for the corpus's seeded secrets.
//
// The rule it enforces has no tolerance band: no seeded secret value appears in
// a snapshot, a model projection, a log, an export, an audit record or a crash
// report. It is one of the invariants that cannot be averaged away.
//
// Three decisions shape the class.
//
// **It scans every token, not the ones a page was supposed to carry.** A token
// that escaped from the wrong fixture is still a leak, and the failure shape
// worth catching is exactly the one where a value crossed from a page nobody
// was looking at.
//
// **It looks for obfuscated forms too.** The protocol requires redaction tests
// to cover obfuscated values, and a redactor that suppressed a plain string
// while a nearby layer emitted the same bytes percent-encoded, hyphen-stripped
// or base64-encoded would pass a literal search and leak anyway. The variants
// are enumerated in the source and each says which channel it defends.
//
// **It names the sink.** A finding says which sink the value reached, because
// "a canary leaked" is not actionable and "the canary reached the audit stream"
// is. Testing and delivery is explicit that the user interface hiding a value
// is not evidence that the value stayed on the device, so a suite adds every
// sink it can reach and asserts once.

class CanaryLeakScanner {
 public:
  // Every canary the corpus declares. This is the form a suite should use
  // unless it has a specific reason not to.
  static CanaryLeakScanner ForWholeCorpus();

  // Every canary plus the declared sensitive omissions of one fixture. Use it
  // when the fixture names values that are not canaries — a test card number,
  // for example — and the assertion should cover those too.
  static CanaryLeakScanner ForFixture(std::string_view fixture_id);

  explicit CanaryLeakScanner(std::vector<std::string> prohibited_values);
  CanaryLeakScanner(const CanaryLeakScanner&) = delete;
  CanaryLeakScanner& operator=(const CanaryLeakScanner&) = delete;
  CanaryLeakScanner(CanaryLeakScanner&&) noexcept;
  ~CanaryLeakScanner();

  // Registers one egress sink's entire content under a readable name.
  void AddSink(std::string name, std::string content);

  // Registers the identifier fields of every record in a journal or an audit
  // stream. Both are content free by construction, so what this proves is that
  // no secret reached them through an identifier, which is the one route a
  // trivially-copyable record still leaves open.
  void AddJournal(const RecordingJournal& journal);
  void AddAuditStream(const RecordingAuditStream& stream);

  // The prohibited values found in `text`, in declaration order. Empty is
  // clean. Exposed so a test can assert on one string without registering it.
  std::vector<std::string> Scan(std::string_view text) const;

  // Fails with the sink name, the value, and the variant that matched.
  [[nodiscard]] ::testing::AssertionResult AssertAllSinksClean() const;

  size_t prohibited_value_count() const { return prohibited_values_.size(); }
  size_t sink_count() const { return sinks_.size(); }

 private:
  struct Sink {
    std::string name;
    std::string content;
  };

  // Every spelling of one prohibited value that a sink might carry, paired with
  // the reason that spelling is checked.
  static std::vector<std::pair<std::string, std::string>> VariantsOf(
      const std::string& value);

  std::vector<std::string> prohibited_values_;
  std::vector<Sink> sinks_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_CANARY_LEAK_SCANNER_H_
