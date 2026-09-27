// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMMON_PUBLIC_PAGE_INSPECTOR_PROJECTION_H_
#define TAFFY_COMMON_PUBLIC_PAGE_INSPECTOR_PROJECTION_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

namespace taffy {

// Browser-redacted, projection-local page structure. These POD values are an
// internal staging shape: Android and desktop consume only the generated Core
// API records. No renderer identity, BIP enum, URL, capability, or policy
// digest is representable here.
enum class InspectorNodeRole : uint8_t {
  kDocument = 0,
  kSection = 1,
  kText = 2,
  kList = 3,
  kTable = 4,
  kLink = 5,
  kControl = 6,
  kMedia = 7,
  kCommerce = 8,
  kReference = 9,
  kUnknown = 10,
};

enum class InspectorSensitivity : uint8_t {
  kPublic = 0,
  kWithheld = 1,
  kUnknown = 2,
};

enum class InspectorRelationship : uint8_t {
  kHierarchy = 0,
  kLabel = 1,
  kDescription = 2,
  kControl = 3,
  kTableHeader = 4,
  kEntity = 5,
  kSource = 6,
  kOther = 7,
};

struct InspectorNodeProjection {
  std::string display_id;
  InspectorNodeRole role = InspectorNodeRole::kUnknown;
  std::optional<std::string> name;
  InspectorSensitivity sensitivity = InspectorSensitivity::kUnknown;
  uint32_t text_run_count = 0;
  uint64_t text_byte_count = 0;
  bool value_present = false;
  bool value_withheld = false;
};

struct InspectorEdgeProjection {
  std::string from_display_id;
  std::string to_display_id;
  InspectorRelationship relationship = InspectorRelationship::kOther;
  bool inferred = false;
};

struct InspectorGraphProjection {
  std::vector<InspectorNodeProjection> nodes;
  std::vector<InspectorEdgeProjection> edges;
};

}  // namespace taffy

#endif  // TAFFY_COMMON_PUBLIC_PAGE_INSPECTOR_PROJECTION_H_
