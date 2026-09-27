// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/adapter_requirement_check.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// "Missing required support returns UNSUPPORTED, not a degraded result that
// looks complete" (protocol section 6.2), and "adapter failure is isolated and
// named; one missing optional adapter need not discard all other evidence"
// (protocol section 15).

namespace taffy {
namespace {

AdapterRequirement Require(AdapterKind adapter) {
  return AdapterRequirement{adapter, AdapterRequirementLevel::kRequired};
}

AdapterRequirement Optional(AdapterKind adapter) {
  return AdapterRequirement{adapter, AdapterRequirementLevel::kOptional};
}

AdapterReport Report(AdapterKind adapter, AdapterStatus status) {
  AdapterReport report;
  report.adapter = adapter;
  report.status = status;
  return report;
}

ProtocolSupportEnvelope Supporting(std::vector<AdapterKind> adapters) {
  ProtocolSupportEnvelope envelope;
  envelope.code = ObservationResultCode::kOk;
  for (AdapterKind adapter : adapters) {
    AdapterSupport support;
    support.adapter = adapter;
    support.supported = true;
    envelope.adapters.push_back(support);
  }
  return envelope;
}

TEST(AdapterRequirementCheckTest, AMissingRequiredAdapterIsUnsupported) {
  const AdapterRequirementOutcome outcome = CheckAdaptersAgainstSupport(
      {Require(AdapterKind::kAccessibility)}, Supporting({AdapterKind::kDom}),
      {AdapterKind::kDom, AdapterKind::kAccessibility});
  EXPECT_EQ(outcome.code, ObservationResultCode::kUnsupported);
  EXPECT_EQ(outcome.required_missing_count, 1u);
  ASSERT_EQ(outcome.findings.size(), 1u);
  EXPECT_EQ(outcome.findings.front().disposition,
            AdapterDisposition::kUnsupportedByEndpoint);
}

TEST(AdapterRequirementCheckTest, AnUngrantedAdapterIsNamedSeparately) {
  const AdapterRequirementOutcome outcome = CheckAdaptersAgainstSupport(
      {Optional(AdapterKind::kMetadata)},
      Supporting({AdapterKind::kDom, AdapterKind::kMetadata}),
      {AdapterKind::kDom});
  // A policy decision and a capability gap are different problems, and telling
  // them apart without reading the grant is the point of the distinction.
  ASSERT_EQ(outcome.findings.size(), 1u);
  EXPECT_EQ(outcome.findings.front().disposition,
            AdapterDisposition::kNotGranted);
  EXPECT_EQ(outcome.code, ObservationResultCode::kOk);
}

TEST(AdapterRequirementCheckTest, AMissingOptionalAdapterKeepsTheRest) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Optional(AdapterKind::kMetadata)},
      {Report(AdapterKind::kDom, AdapterStatus::kOk)},
      /*observation_was_truncated=*/false);
  // The optional absence is named, while the required DOM evidence remains a
  // complete usable result.
  EXPECT_EQ(outcome.code, ObservationResultCode::kOk);
  EXPECT_EQ(outcome.required_missing_count, 0u);
  EXPECT_EQ(outcome.optional_missing_count, 1u);
}

TEST(AdapterRequirementCheckTest,
     AnUnsupportedOptionalAdapterIsNamedWithoutDegradingTheResult) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Optional(AdapterKind::kForms)},
      {Report(AdapterKind::kDom, AdapterStatus::kOk),
       Report(AdapterKind::kForms, AdapterStatus::kUnsupported)},
      /*observation_was_truncated=*/false);
  EXPECT_EQ(outcome.code, ObservationResultCode::kOk);
  EXPECT_EQ(outcome.optional_missing_count, 1u);
  ASSERT_EQ(outcome.findings.size(), 2u);
  EXPECT_EQ(outcome.findings[1].disposition,
            AdapterDisposition::kUnsupportedForDocument);
}

TEST(AdapterRequirementCheckTest,
     APartialOptionalAdapterStillMakesTheResultIncomplete) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Optional(AdapterKind::kMetadata)},
      {Report(AdapterKind::kDom, AdapterStatus::kOk),
       Report(AdapterKind::kMetadata, AdapterStatus::kIncomplete)},
      /*observation_was_truncated=*/false);
  EXPECT_EQ(outcome.code, ObservationResultCode::kIncomplete);
  EXPECT_EQ(outcome.incomplete_count, 1u);
  EXPECT_EQ(outcome.optional_missing_count, 0u);
}

TEST(AdapterRequirementCheckTest,
     APartialRequiredAdapterIsIncompleteNotUnsupported) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom)},
      {Report(AdapterKind::kDom, AdapterStatus::kIncomplete)},
      /*observation_was_truncated=*/false);
  // The DOM adapter walked the document and stopped at a boundary it is not
  // allowed to cross — an open or closed shadow root, a cross-origin frame, a
  // budget. It ran, and a graph came back. UNSUPPORTED would tell a caller
  // holding that graph that the browser cannot serve the request at all.
  EXPECT_EQ(outcome.code, ObservationResultCode::kIncomplete);
  EXPECT_EQ(outcome.required_missing_count, 0u);
  EXPECT_EQ(outcome.optional_missing_count, 0u);
  EXPECT_EQ(outcome.incomplete_count, 1u);
  // Degraded is never silent: the finding names the adapter and the envelope
  // carries a warning for it.
  ASSERT_EQ(outcome.findings.size(), 1u);
  EXPECT_EQ(outcome.findings.front().disposition,
            AdapterDisposition::kIncomplete);
  EXPECT_FALSE(outcome.warning_codes.empty());
}

TEST(AdapterRequirementCheckTest,
     APartialAdapterDoesNotMaskAnAbsentRequiredOne) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Require(AdapterKind::kAccessibility)},
      {Report(AdapterKind::kDom, AdapterStatus::kIncomplete)},
      /*observation_was_truncated=*/false);
  // Softening the partial case must not soften the absent one. The
  // accessibility adapter was required and never reported, and that is still
  // fatal to the request.
  EXPECT_EQ(outcome.code, ObservationResultCode::kUnsupported);
  EXPECT_EQ(outcome.required_missing_count, 1u);
  EXPECT_EQ(outcome.incomplete_count, 1u);
}

TEST(AdapterRequirementCheckTest, SilenceIsNotSuccess) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kForms)}, {}, /*observation_was_truncated=*/false);
  // An adapter that ran would have said so, so an unmentioned one did not.
  EXPECT_EQ(outcome.code, ObservationResultCode::kUnsupported);
}

TEST(AdapterRequirementCheckTest,
     ATruncatedReplyMakesAnUnmentionedRequiredAdapterIncomplete) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kAccessibility), Require(AdapterKind::kDom)},
      {Report(AdapterKind::kAccessibility, AdapterStatus::kIncomplete)},
      /*observation_was_truncated=*/true);
  // The shape a phone produced on a search results page: the accessibility
  // adapter exhausted the depth budget, collection stopped, and the DOM
  // adapter never ran. A 195-node graph came back and the task was told the
  // browser cannot serve the request at all.
  EXPECT_EQ(outcome.code, ObservationResultCode::kIncomplete);
  EXPECT_EQ(outcome.required_missing_count, 0u);
  EXPECT_EQ(outcome.incomplete_count, 2u);
  ASSERT_EQ(outcome.findings.size(), 2u);
  EXPECT_EQ(outcome.findings[1].disposition, AdapterDisposition::kIncomplete);
  // Degraded is never silent.
  EXPECT_FALSE(outcome.warning_codes.empty());
}

TEST(AdapterRequirementCheckTest, AnUntruncatedReplyStillReadsSilenceAsAbsent) {
  const AdapterRequirementOutcome outcome =
      CheckAdapterReports({Require(AdapterKind::kDom)}, {},
                          /*observation_was_truncated=*/false);
  // Nothing cut this reply short, so an adapter that never spoke is an
  // adapter that was not there to run.
  EXPECT_EQ(outcome.code, ObservationResultCode::kUnsupported);
  EXPECT_EQ(outcome.required_missing_count, 1u);
}

TEST(AdapterRequirementCheckTest, AFailedAdapterIsIsolatedAndNamed) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Optional(AdapterKind::kMetadata)},
      {Report(AdapterKind::kDom, AdapterStatus::kOk),
       Report(AdapterKind::kMetadata, AdapterStatus::kFailed)},
      /*observation_was_truncated=*/false);
  EXPECT_EQ(outcome.code, ObservationResultCode::kOk);
  EXPECT_EQ(outcome.failed_count, 1u);
  EXPECT_FALSE(outcome.warning_codes.empty());
  ASSERT_EQ(outcome.findings.size(), 2u);
  EXPECT_EQ(outcome.findings[0].disposition, AdapterDisposition::kSatisfied);
  EXPECT_EQ(outcome.findings[1].disposition, AdapterDisposition::kFailed);
}

TEST(AdapterRequirementCheckTest, AFailedRequiredAdapterIsUnsupported) {
  const AdapterRequirementOutcome outcome =
      CheckAdapterReports({Require(AdapterKind::kDom)},
                          {Report(AdapterKind::kDom, AdapterStatus::kFailed)},
                          /*observation_was_truncated=*/false);
  EXPECT_EQ(outcome.code, ObservationResultCode::kUnsupported);
  EXPECT_EQ(outcome.required_missing_count, 1u);
  EXPECT_EQ(outcome.failed_count, 1u);
}

TEST(AdapterRequirementCheckTest, DisagreementIsReportedNotResolved) {
  const AdapterRequirementOutcome outcome = CheckAdapterReports(
      {Require(AdapterKind::kDom), Require(AdapterKind::kMetadata)},
      {Report(AdapterKind::kDom, AdapterStatus::kOk),
       Report(AdapterKind::kMetadata, AdapterStatus::kConflicted)},
      /*observation_was_truncated=*/false);
  // Conflicting evidence is preserved; a consumer is told the graph carries
  // competing candidates rather than one adapter's answer.
  EXPECT_EQ(outcome.code, ObservationResultCode::kConflicted);
  EXPECT_EQ(outcome.conflicted_count, 1u);
  EXPECT_EQ(outcome.required_missing_count, 0u);
}

TEST(AdapterRequirementCheckTest, EverythingPresentIsOk) {
  const AdapterRequirementOutcome outcome =
      CheckAdapterReports({Require(AdapterKind::kDom)},
                          {Report(AdapterKind::kDom, AdapterStatus::kOk)},
                          /*observation_was_truncated=*/false);
  EXPECT_EQ(outcome.code, ObservationResultCode::kOk);
}

TEST(AdapterRequirementCheckTest, CombiningCodesTakesTheStricterOne) {
  EXPECT_EQ(CombineObservationCodes(ObservationResultCode::kOk,
                                    ObservationResultCode::kIncomplete),
            ObservationResultCode::kIncomplete);
  // A budget failure is not softened by an adapter result that was merely
  // incomplete.
  EXPECT_EQ(CombineObservationCodes(ObservationResultCode::kBudgetExceeded,
                                    ObservationResultCode::kIncomplete),
            ObservationResultCode::kBudgetExceeded);
  EXPECT_EQ(CombineObservationCodes(ObservationResultCode::kIncomplete,
                                    ObservationResultCode::kUnsupported),
            ObservationResultCode::kUnsupported);
}

}  // namespace
}  // namespace taffy
