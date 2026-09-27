// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/adapter_registry.h"

#include <utility>

#include "taffy/renderer/adapters/accessibility_adapter.h"
#include "taffy/renderer/adapters/document_metadata_adapter.h"
#include "taffy/renderer/adapters/dom_adapter.h"
#include "taffy/renderer/adapters/form_schema_adapter.h"
#include "taffy/renderer/adapters/layout_visibility_adapter.h"
#include "taffy/renderer/adapters/selection_adapter.h"
#include "taffy/renderer/adapters/structured_data_adapter.h"

namespace taffy {

namespace {

AdapterRegistry::Entry MakeEntry(AdapterKind kind,
                                 std::unique_ptr<Adapter> adapter) {
  AdapterRegistry::Entry entry;
  entry.kind = kind;
  entry.adapter = std::move(adapter);
  entry.precedence = AdapterRegistry::PrecedenceOf(kind);
  return entry;
}

}  // namespace

AdapterRegistry::Entry::Entry() = default;
AdapterRegistry::Entry::Entry(Entry&&) = default;
AdapterRegistry::Entry& AdapterRegistry::Entry::operator=(Entry&&) = default;
AdapterRegistry::Entry::~Entry() = default;

// static
std::vector<AdapterRegistry::Entry> AdapterRegistry::CreateAll() {
  std::vector<Entry> adapters;

  // 1. Browser-owned document identity and lifecycle. First, and first for a
  //    reason beyond precedence: it allocates ordinal zero of the derived
  //    identity space, so the document node is deterministically the same
  //    identity in every extraction of one epoch.
  adapters.push_back(MakeEntry(AdapterKind::kDocumentMetadata,
                               std::make_unique<DocumentMetadataAdapter>()));

  // 2. What a user can perceive and operate. Also the shadow path
  //    (protocol section 8.2).
  adapters.push_back(MakeEntry(AdapterKind::kAccessibility,
                               std::make_unique<AccessibilityAdapter>()));

  // 3. Typed control semantics. Read-only, and no values.
  adapters.push_back(
      MakeEntry(AdapterKind::kForms, std::make_unique<FormSchemaAdapter>()));

  // 4. Structured metadata, with the freshness and visibility checks protocol
  //    section 7.6 asks for. It runs AFTER accessibility on purpose: the
  //    corroboration test needs perceivable content to check against, and
  //    accessibility is the higher-precedence source of exactly that.
  adapters.push_back(MakeEntry(AdapterKind::kStructuredData,
                               std::make_unique<StructuredDataAdapter>()));

  // 5. Raw DOM structure and text. The fallback, not the authority.
  adapters.push_back(
      MakeEntry(AdapterKind::kDom, std::make_unique<DomAdapter>()));

  // 6-7. The annotators. Selection before layout because a selection-scoped
  //      request narrows the node set, and narrowing before the occlusion
  //      probes run means the probe budget is spent on nodes the caller
  //      actually asked about.
  adapters.push_back(MakeEntry(AdapterKind::kSelection,
                               std::make_unique<SelectionAdapter>()));
  adapters.push_back(MakeEntry(AdapterKind::kLayout,
                               std::make_unique<LayoutVisibilityAdapter>()));

  return adapters;
}

// static
std::string_view AdapterRegistry::NameOf(AdapterKind kind) {
  switch (kind) {
    case AdapterKind::kDom:
      return "dom";
    case AdapterKind::kAccessibility:
      return "accessibility";
    case AdapterKind::kForms:
      return "form-schema";
    case AdapterKind::kStructuredData:
      return "structured-data";
    case AdapterKind::kDocumentMetadata:
      return "document-metadata";
    case AdapterKind::kSelection:
      return "selection";
    case AdapterKind::kLayout:
      return "layout";
  }
}

// static
uint32_t AdapterRegistry::PrecedenceOf(AdapterKind kind) {
  // Protocol section 7.6, numbered as the specification numbers it. Item 6 -
  // targeted screenshot and vision - has no adapter here and is not
  // advertised.
  switch (kind) {
    case AdapterKind::kDocumentMetadata:
      return 1;
    case AdapterKind::kAccessibility:
      return 2;
    case AdapterKind::kForms:
      return 3;
    case AdapterKind::kStructuredData:
      return 4;
    case AdapterKind::kDom:
      return 5;
    case AdapterKind::kSelection:
    case AdapterKind::kLayout:
      // Not sources of field values. An annotator states that a node is
      // selected or obscured; it never states what a field's value is, so it
      // has no place in a precedence argument about one.
      return 0;
  }
}

}  // namespace taffy
