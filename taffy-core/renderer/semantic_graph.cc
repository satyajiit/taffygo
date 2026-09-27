// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph.h"

#include <utility>

// Out-of-line special members: Chromium's clang plugin requires them for any
// class with non-trivial members, and inlining them into every adapter
// translation unit is exactly the bloat that rule exists to prevent.

namespace taffy {

FieldEvidence::FieldEvidence() = default;

FieldEvidence::FieldEvidence(SemanticField field,
                             SourceKind source_kind,
                             std::string source_locator,
                             uint32_t extraction_rule_version,
                             Transformation transformation)
    : field(field),
      source_kind(source_kind),
      source_locator(std::move(source_locator)),
      extraction_rule_version(extraction_rule_version),
      transformation(transformation) {}

FieldEvidence::FieldEvidence(const FieldEvidence&) = default;
FieldEvidence::FieldEvidence(FieldEvidence&&) = default;
FieldEvidence& FieldEvidence::operator=(const FieldEvidence&) = default;
FieldEvidence& FieldEvidence::operator=(FieldEvidence&&) = default;
FieldEvidence::~FieldEvidence() = default;

TextRun::TextRun() = default;
TextRun::TextRun(const TextRun&) = default;
TextRun::TextRun(TextRun&&) = default;
TextRun& TextRun::operator=(const TextRun&) = default;
TextRun& TextRun::operator=(TextRun&&) = default;
TextRun::~TextRun() = default;

ValueDescriptor::ValueDescriptor() = default;
ValueDescriptor::ValueDescriptor(const ValueDescriptor&) = default;
ValueDescriptor::ValueDescriptor(ValueDescriptor&&) = default;
ValueDescriptor& ValueDescriptor::operator=(const ValueDescriptor&) = default;
ValueDescriptor& ValueDescriptor::operator=(ValueDescriptor&&) = default;
ValueDescriptor::~ValueDescriptor() = default;

Attribute::Attribute() = default;

Attribute::Attribute(AttributeKey key, std::string value)
    : key(key), value(std::move(value)) {}

Attribute::Attribute(const Attribute&) = default;
Attribute::Attribute(Attribute&&) = default;
Attribute& Attribute::operator=(const Attribute&) = default;
Attribute& Attribute::operator=(Attribute&&) = default;
Attribute::~Attribute() = default;

// In the mirror namespace, because that is where the class is declared: an
// out-of-line member definition has to appear in a namespace enclosing its
// class, and the using-declaration in the header does not make `taffy` one.
namespace renderer {

Destination::Destination() = default;
Destination::Destination(const Destination&) = default;
Destination::Destination(Destination&&) = default;
Destination& Destination::operator=(const Destination&) = default;
Destination& Destination::operator=(Destination&&) = default;
Destination::~Destination() = default;

}  // namespace renderer

SemanticNode::SemanticNode() = default;
SemanticNode::SemanticNode(const SemanticNode&) = default;
SemanticNode::SemanticNode(SemanticNode&&) = default;
SemanticNode& SemanticNode::operator=(const SemanticNode&) = default;
SemanticNode& SemanticNode::operator=(SemanticNode&&) = default;
SemanticNode::~SemanticNode() = default;

SemanticEdge::SemanticEdge() = default;
SemanticEdge::SemanticEdge(const SemanticEdge&) = default;
SemanticEdge::SemanticEdge(SemanticEdge&&) = default;
SemanticEdge& SemanticEdge::operator=(const SemanticEdge&) = default;
SemanticEdge& SemanticEdge::operator=(SemanticEdge&&) = default;
SemanticEdge::~SemanticEdge() = default;

ObservationWarning::ObservationWarning() = default;

ObservationWarning::ObservationWarning(WarningCode code,
                                       std::string detail_code)
    : code(code), detail_code(std::move(detail_code)) {}

ObservationWarning::ObservationWarning(const ObservationWarning&) = default;
ObservationWarning::ObservationWarning(ObservationWarning&&) = default;
ObservationWarning& ObservationWarning::operator=(const ObservationWarning&) =
    default;
ObservationWarning& ObservationWarning::operator=(ObservationWarning&&) =
    default;
ObservationWarning::~ObservationWarning() = default;

}  // namespace taffy
