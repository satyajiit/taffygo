// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/page_capabilities.h"

#include "taffy/renderer/adapters/adapter_registry.h"
#include "taffy/renderer/wire_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

// CAP-PI-008. The property under test is honesty, and it has three parts:
// every adapter always has an entry, a run can only ever lower a standing,
// and a scope nothing can serve is never advertised.

namespace taffy {
namespace {

DocumentCapabilitySignals RichDocument() {
  DocumentCapabilitySignals signals;
  signals.has_document = true;
  signals.has_body = true;
  signals.has_browser_assigned_epoch = true;
  signals.has_browser_origin_set = true;
  signals.lifecycle_is_active = true;
  signals.accessibility_available = true;
  signals.viewport_geometry_available = true;
  signals.has_selection = true;
  signals.has_form_controls = true;
  signals.has_structured_data = true;
  return signals;
}

TEST(PageCapabilitiesTest, EveryAdapterAlwaysHasAnEntry) {
  // An omitted adapter reads as "not asked about". An unsupported one reads
  // as "asked, and the answer is no". Only the second is honest.
  const PageCapabilities empty =
      PageCapabilities::ForDocument(DocumentCapabilitySignals());
  EXPECT_EQ(empty.adapters().size(), 7u);
  for (const AdapterCapability& capability : empty.adapters()) {
    EXPECT_EQ(capability.availability, AdapterAvailability::kUnsupported);
    EXPECT_FALSE(capability.detail_code.empty())
        << "an unsupported adapter must say why";
  }
}

TEST(PageCapabilitiesTest, RichDocumentSupportsEverything) {
  const PageCapabilities capabilities =
      PageCapabilities::ForDocument(RichDocument());
  for (const AdapterCapability& capability : capabilities.adapters()) {
    EXPECT_EQ(capability.availability, AdapterAvailability::kPresent)
        << capability.name;
  }
  EXPECT_TRUE(capabilities.AnyProducerAvailable());
}

TEST(PageCapabilitiesTest, NamesMatchTheRegistry) {
  const PageCapabilities capabilities =
      PageCapabilities::ForDocument(RichDocument());
  for (const AdapterCapability& capability : capabilities.adapters()) {
    EXPECT_EQ(capability.name, AdapterRegistry::NameOf(capability.kind));
    EXPECT_EQ(capability.precedence,
              AdapterRegistry::PrecedenceOf(capability.kind));
  }
}

TEST(PageCapabilitiesTest, RuleVersionsMatchTheAdaptersThemselves) {
  // The capability report states each adapter's extraction rule version
  // without constructing one. That is a second copy, and this is the test
  // that proves the two agree - which is the only thing that makes a second
  // copy acceptable.
  const PageCapabilities capabilities =
      PageCapabilities::ForDocument(RichDocument());
  for (const AdapterRegistry::Entry& entry : AdapterRegistry::CreateAll()) {
    EXPECT_EQ(capabilities.For(entry.kind).extraction_rule_version,
              entry.adapter->extraction_rule_version())
        << AdapterRegistry::NameOf(entry.kind);
  }
}

TEST(PageCapabilitiesTest, MissingSignalsProduceNamedRefusals) {
  DocumentCapabilitySignals signals = RichDocument();
  signals.has_selection = false;
  signals.has_structured_data = false;
  signals.viewport_geometry_available = false;

  const PageCapabilities capabilities = PageCapabilities::ForDocument(signals);
  EXPECT_EQ(capabilities.For(AdapterKind::kSelection).availability,
            AdapterAvailability::kUnsupported);
  EXPECT_EQ(capabilities.For(AdapterKind::kSelection).detail_code,
            capability_reason::kNoSelection);
  EXPECT_EQ(capabilities.For(AdapterKind::kStructuredData).availability,
            AdapterAvailability::kUnsupported);
  EXPECT_EQ(capabilities.For(AdapterKind::kLayout).availability,
            AdapterAvailability::kUnsupported);
  // The producing adapters are unaffected: one adapter failing is isolated
  // (protocol section 15).
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kPresent);
  EXPECT_EQ(capabilities.For(AdapterKind::kAccessibility).availability,
            AdapterAvailability::kPresent);
}

TEST(PageCapabilitiesTest, LayoutIsDegradedWithoutAnAccessibilityTree) {
  // Bounds can still be measured; occlusion cannot be probed, because the
  // probe is an accessibility hit test. Degraded says exactly that, and the
  // precondition check refuses an anti-occlusion precondition for every node
  // as a result.
  DocumentCapabilitySignals signals = RichDocument();
  signals.accessibility_available = false;
  const PageCapabilities capabilities = PageCapabilities::ForDocument(signals);
  EXPECT_EQ(capabilities.For(AdapterKind::kLayout).availability,
            AdapterAvailability::kDegraded);
  EXPECT_EQ(capabilities.For(AdapterKind::kAccessibility).availability,
            AdapterAvailability::kUnsupported);
  EXPECT_EQ(capabilities.For(AdapterKind::kSelection).availability,
            AdapterAvailability::kUnsupported);
}

TEST(PageCapabilitiesTest, ARunCanOnlyLowerAStanding) {
  PageCapabilities capabilities = PageCapabilities::ForDocument(RichDocument());
  ASSERT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kPresent);

  capabilities.RecordRunOutcome(AdapterKind::kDom, AdapterStatus::kIncomplete);
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kDegraded);
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).detail_code,
            capability_reason::kRunIncomplete);

  // A later kOk does not undo it: a run cannot prove a capability the
  // document did not have, and a partial answer stays partial.
  capabilities.RecordRunOutcome(AdapterKind::kDom, AdapterStatus::kOk);
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kDegraded);

  capabilities.RecordRunOutcome(AdapterKind::kDom, AdapterStatus::kFailed);
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kUnsupported);
  capabilities.RecordRunOutcome(AdapterKind::kDom, AdapterStatus::kOk);
  EXPECT_EQ(capabilities.For(AdapterKind::kDom).availability,
            AdapterAvailability::kUnsupported);
}

TEST(PageCapabilitiesTest, ConflictedIsDegradedNotFailed) {
  // Preserving a disagreement is the correct outcome (protocol section 7.5).
  // It is degraded only because a caller must not read a conflicted answer as
  // a settled one.
  PageCapabilities capabilities = PageCapabilities::ForDocument(RichDocument());
  capabilities.RecordRunOutcome(AdapterKind::kStructuredData,
                                AdapterStatus::kConflicted);
  EXPECT_EQ(capabilities.For(AdapterKind::kStructuredData).availability,
            AdapterAvailability::kDegraded);
  EXPECT_EQ(capabilities.For(AdapterKind::kStructuredData).detail_code,
            capability_reason::kRunConflicted);
}

TEST(PageCapabilitiesTest, SectionScopeRequiresFormSemantics) {
  // BIP 0.9 names an exact form root for SECTION. Capability advertisement
  // says whether the document can describe forms; SnapshotBuilder separately
  // proves that the particular request still names one live form.
  const PageCapabilities capabilities =
      PageCapabilities::ForDocument(RichDocument());
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kSection));
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kDocument));
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kInteractive));
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kViewport));
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kSelection));
}

TEST(PageCapabilitiesTest, ScopesFollowTheAdaptersTheyNeed) {
  DocumentCapabilitySignals signals = RichDocument();
  signals.has_selection = false;
  signals.viewport_geometry_available = false;
  signals.has_form_controls = false;
  const PageCapabilities capabilities = PageCapabilities::ForDocument(signals);
  EXPECT_FALSE(capabilities.SupportsScope(ExtractionScope::kSelection));
  EXPECT_FALSE(capabilities.SupportsScope(ExtractionScope::kViewport));
  EXPECT_FALSE(capabilities.SupportsScope(ExtractionScope::kSection));
  EXPECT_TRUE(capabilities.SupportsScope(ExtractionScope::kDocument));
}

TEST(PageCapabilitiesTest, EveryAdapterCanBeNamedOnTheWire) {
  // Selection and layout once had no mojom::AdapterKind member, so this
  // module marked them and the endpoint described them in a warning. The
  // additive minor contract step recorded in
  // taffy-core/contracts/bip/schema/bip.version.json appended SELECTION and
  // LAYOUT, so the mapping is total and every adapter is reported the same way.
  // This test is what keeps that true: a kind added here with no wire member
  // would fail to compile against a total ToMojom, and a kind mapped to the
  // wrong member fails below.
  const PageCapabilities capabilities =
      PageCapabilities::ForDocument(RichDocument());
  for (const AdapterCapability& capability : capabilities.adapters()) {
    // Round trip, not just "has a member": a kind that reached the wire under
    // somebody else's member would still have one.
    EXPECT_EQ(wire::FromMojom(wire::ToMojom(capability.kind)), capability.kind)
        << capability.name;
  }
  EXPECT_EQ(wire::ToMojom(AdapterKind::kSelection),
            mojom::AdapterKind::kSelection);
  EXPECT_EQ(wire::ToMojom(AdapterKind::kLayout), mojom::AdapterKind::kLayout);
  EXPECT_EQ(wire::FromMojom(mojom::AdapterKind::kSelection),
            AdapterKind::kSelection);
  EXPECT_EQ(wire::FromMojom(mojom::AdapterKind::kLayout), AdapterKind::kLayout);
}

TEST(PageCapabilitiesTest, StructuredDataAndBrowserMapToDistinctWireKinds) {
  // The split of the old metadata adapter into two: JSON-LD and microdata
  // are METADATA, browser-owned document identity is BROWSER. Mapping both to
  // one member would have made the split invisible on the wire.
  EXPECT_EQ(wire::ToMojom(AdapterKind::kStructuredData),
            mojom::AdapterKind::kMetadata);
  EXPECT_EQ(wire::ToMojom(AdapterKind::kDocumentMetadata),
            mojom::AdapterKind::kBrowser);
  EXPECT_EQ(wire::FromMojom(mojom::AdapterKind::kMetadata),
            AdapterKind::kStructuredData);
  EXPECT_EQ(wire::FromMojom(mojom::AdapterKind::kBrowser),
            AdapterKind::kDocumentMetadata);
}

}  // namespace
}  // namespace taffy
