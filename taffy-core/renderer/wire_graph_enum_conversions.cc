// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/notreached.h"
#include "taffy/renderer/wire_conversions.h"

// The semantic-graph half of the enumeration translation: what a node is,
// what state it is in, how nodes relate, and how a value is typed and named.
// This is the vocabulary a model reads the page in, so an unmapped member
// here is a page described wrongly rather than a page described partially.
//
// Every function here is a switch with no default case, and that is the only
// reason this file is worth reading: a member added to the mojom, or to the
// internal mirror in semantic_graph.h, becomes a compile error rather than a
// silent fall-through. For an action type or a sensitivity class, a silent
// fall-through is a security bug rather than a defect.
//
// The struct conversions live in wire_struct_conversions.cc. They are a
// different job - they apply the redaction gate and the URL disclosure rule -
// and mixing them in here buried the exhaustiveness property under several
// hundred lines of field copying.

namespace taffy::wire {

mojom::ContentTrust ToMojom(RendererContentTrust trust) {
  switch (trust) {
    case RendererContentTrust::kFirstPartyDocument:
      return mojom::ContentTrust::kFirstPartyDocument;
    case RendererContentTrust::kUserGeneratedContent:
      return mojom::ContentTrust::kUserGeneratedContent;
    case RendererContentTrust::kThirdPartyEmbedded:
      return mojom::ContentTrust::kThirdPartyEmbedded;
    case RendererContentTrust::kUnknownUntrusted:
      return mojom::ContentTrust::kUnknownUntrusted;
  }
}

mojom::ContentSignal ToMojom(ContentSignal signal) {
  switch (signal) {
    case ContentSignal::kHiddenByStyle:
      return mojom::ContentSignal::kHiddenByStyle;
    case ContentSignal::kZeroWidthCharacters:
      return mojom::ContentSignal::kZeroWidthCharacters;
    case ContentSignal::kBidiControlCharacters:
      return mojom::ContentSignal::kBidiControlCharacters;
    case ContentSignal::kEncodedBlob:
      return mojom::ContentSignal::kEncodedBlob;
    case ContentSignal::kImperativeInstructionShape:
      return mojom::ContentSignal::kImperativeInstructionShape;
    case ContentSignal::kLanguageMismatch:
      return mojom::ContentSignal::kLanguageMismatch;
    case ContentSignal::kCrossOriginFrameAuthored:
      return mojom::ContentSignal::kCrossOriginFrameAuthored;
  }
}

mojom::SemanticField ToMojom(SemanticField field) {
  switch (field) {
    case SemanticField::kRole:
      return mojom::SemanticField::kRole;
    case SemanticField::kName:
      return mojom::SemanticField::kName;
    case SemanticField::kDescription:
      return mojom::SemanticField::kDescription;
    case SemanticField::kTextRuns:
      return mojom::SemanticField::kTextRuns;
    case SemanticField::kStates:
      return mojom::SemanticField::kStates;
    case SemanticField::kValueDescriptor:
      return mojom::SemanticField::kValueDescriptor;
    case SemanticField::kDestination:
      return mojom::SemanticField::kDestination;
    case SemanticField::kBounds:
      return mojom::SemanticField::kBounds;
    case SemanticField::kActions:
      return mojom::SemanticField::kActions;
    case SemanticField::kAttributes:
      return mojom::SemanticField::kAttributes;
    case SemanticField::kSensitivity:
      return mojom::SemanticField::kSensitivity;
    case SemanticField::kEdges:
      return mojom::SemanticField::kEdges;
    case SemanticField::kFrames:
      return mojom::SemanticField::kFrames;
    case SemanticField::kContentTrust:
      return mojom::SemanticField::kContentTrust;
    case SemanticField::kContentSignals:
      return mojom::SemanticField::kContentSignals;
    case SemanticField::kChallengeKind:
      return mojom::SemanticField::kChallengeKind;
  }
}

mojom::SemanticRole ToMojom(SemanticRole role) {
  switch (role) {
    case SemanticRole::kDocument:
      return mojom::SemanticRole::kDocument;
    case SemanticRole::kRegion:
      return mojom::SemanticRole::kRegion;
    case SemanticRole::kHeading:
      return mojom::SemanticRole::kHeading;
    case SemanticRole::kParagraph:
      return mojom::SemanticRole::kParagraph;
    case SemanticRole::kList:
      return mojom::SemanticRole::kList;
    case SemanticRole::kListItem:
      return mojom::SemanticRole::kListItem;
    case SemanticRole::kTable:
      return mojom::SemanticRole::kTable;
    case SemanticRole::kTableRow:
      return mojom::SemanticRole::kTableRow;
    case SemanticRole::kTableCell:
      return mojom::SemanticRole::kTableCell;
    case SemanticRole::kLink:
      return mojom::SemanticRole::kLink;
    case SemanticRole::kButton:
      return mojom::SemanticRole::kButton;
    case SemanticRole::kSearchField:
      return mojom::SemanticRole::kSearchField;
    case SemanticRole::kTextField:
      return mojom::SemanticRole::kTextField;
    case SemanticRole::kCheckbox:
      return mojom::SemanticRole::kCheckbox;
    case SemanticRole::kRadio:
      return mojom::SemanticRole::kRadio;
    case SemanticRole::kSelect:
      return mojom::SemanticRole::kSelect;
    case SemanticRole::kOption:
      return mojom::SemanticRole::kOption;
    case SemanticRole::kImage:
      return mojom::SemanticRole::kImage;
    case SemanticRole::kMedia:
      return mojom::SemanticRole::kMedia;
    case SemanticRole::kPrice:
      return mojom::SemanticRole::kPrice;
    case SemanticRole::kRating:
      return mojom::SemanticRole::kRating;
    case SemanticRole::kAvailability:
      return mojom::SemanticRole::kAvailability;
    case SemanticRole::kLabelValuePair:
      return mojom::SemanticRole::kLabelValuePair;
    case SemanticRole::kCitation:
      return mojom::SemanticRole::kCitation;
    case SemanticRole::kUnknownInteractive:
      return mojom::SemanticRole::kUnknownInteractive;
    case SemanticRole::kUnknownContent:
      return mojom::SemanticRole::kUnknownContent;
  }
}

SemanticRole FromMojom(mojom::SemanticRole role) {
  switch (role) {
    case mojom::SemanticRole::kDocument:
      return SemanticRole::kDocument;
    case mojom::SemanticRole::kRegion:
      return SemanticRole::kRegion;
    case mojom::SemanticRole::kHeading:
      return SemanticRole::kHeading;
    case mojom::SemanticRole::kParagraph:
      return SemanticRole::kParagraph;
    case mojom::SemanticRole::kList:
      return SemanticRole::kList;
    case mojom::SemanticRole::kListItem:
      return SemanticRole::kListItem;
    case mojom::SemanticRole::kTable:
      return SemanticRole::kTable;
    case mojom::SemanticRole::kTableRow:
      return SemanticRole::kTableRow;
    case mojom::SemanticRole::kTableCell:
      return SemanticRole::kTableCell;
    case mojom::SemanticRole::kLink:
      return SemanticRole::kLink;
    case mojom::SemanticRole::kButton:
      return SemanticRole::kButton;
    case mojom::SemanticRole::kSearchField:
      return SemanticRole::kSearchField;
    case mojom::SemanticRole::kTextField:
      return SemanticRole::kTextField;
    case mojom::SemanticRole::kCheckbox:
      return SemanticRole::kCheckbox;
    case mojom::SemanticRole::kRadio:
      return SemanticRole::kRadio;
    case mojom::SemanticRole::kSelect:
      return SemanticRole::kSelect;
    case mojom::SemanticRole::kOption:
      return SemanticRole::kOption;
    case mojom::SemanticRole::kImage:
      return SemanticRole::kImage;
    case mojom::SemanticRole::kMedia:
      return SemanticRole::kMedia;
    case mojom::SemanticRole::kPrice:
      return SemanticRole::kPrice;
    case mojom::SemanticRole::kRating:
      return SemanticRole::kRating;
    case mojom::SemanticRole::kAvailability:
      return SemanticRole::kAvailability;
    case mojom::SemanticRole::kLabelValuePair:
      return SemanticRole::kLabelValuePair;
    case mojom::SemanticRole::kCitation:
      return SemanticRole::kCitation;
    case mojom::SemanticRole::kUnknownInteractive:
      return SemanticRole::kUnknownInteractive;
    case mojom::SemanticRole::kUnknownContent:
      return SemanticRole::kUnknownContent;
  }
}

mojom::NodeState ToMojom(NodeState state) {
  switch (state) {
    case NodeState::kVisible:
      return mojom::NodeState::kVisible;
    case NodeState::kNotVisible:
      return mojom::NodeState::kNotVisible;
    case NodeState::kOffscreen:
      return mojom::NodeState::kOffscreen;
    case NodeState::kObscured:
      return mojom::NodeState::kObscured;
    case NodeState::kEnabled:
      return mojom::NodeState::kEnabled;
    case NodeState::kDisabled:
      return mojom::NodeState::kDisabled;
    case NodeState::kEditable:
      return mojom::NodeState::kEditable;
    case NodeState::kReadOnly:
      return mojom::NodeState::kReadOnly;
    case NodeState::kRequired:
      return mojom::NodeState::kRequired;
    case NodeState::kInvalid:
      return mojom::NodeState::kInvalid;
    case NodeState::kChecked:
      return mojom::NodeState::kChecked;
    case NodeState::kUnchecked:
      return mojom::NodeState::kUnchecked;
    case NodeState::kMixed:
      return mojom::NodeState::kMixed;
    case NodeState::kSelected:
      return mojom::NodeState::kSelected;
    case NodeState::kExpanded:
      return mojom::NodeState::kExpanded;
    case NodeState::kCollapsed:
      return mojom::NodeState::kCollapsed;
    case NodeState::kFocused:
      return mojom::NodeState::kFocused;
    case NodeState::kBusy:
      return mojom::NodeState::kBusy;
  }
}

NodeState FromMojom(mojom::NodeState state) {
  switch (state) {
    case mojom::NodeState::kVisible:
      return NodeState::kVisible;
    case mojom::NodeState::kNotVisible:
      return NodeState::kNotVisible;
    case mojom::NodeState::kOffscreen:
      return NodeState::kOffscreen;
    case mojom::NodeState::kObscured:
      return NodeState::kObscured;
    case mojom::NodeState::kEnabled:
      return NodeState::kEnabled;
    case mojom::NodeState::kDisabled:
      return NodeState::kDisabled;
    case mojom::NodeState::kEditable:
      return NodeState::kEditable;
    case mojom::NodeState::kReadOnly:
      return NodeState::kReadOnly;
    case mojom::NodeState::kRequired:
      return NodeState::kRequired;
    case mojom::NodeState::kInvalid:
      return NodeState::kInvalid;
    case mojom::NodeState::kChecked:
      return NodeState::kChecked;
    case mojom::NodeState::kUnchecked:
      return NodeState::kUnchecked;
    case mojom::NodeState::kMixed:
      return NodeState::kMixed;
    case mojom::NodeState::kSelected:
      return NodeState::kSelected;
    case mojom::NodeState::kExpanded:
      return NodeState::kExpanded;
    case mojom::NodeState::kCollapsed:
      return NodeState::kCollapsed;
    case mojom::NodeState::kFocused:
      return NodeState::kFocused;
    case mojom::NodeState::kBusy:
      return NodeState::kBusy;
  }
}

mojom::RelationshipKind ToMojom(EdgeType type) {
  switch (type) {
    case EdgeType::kContains:
      return mojom::RelationshipKind::kContains;
    case EdgeType::kLabels:
      return mojom::RelationshipKind::kLabels;
    case EdgeType::kDescribes:
      return mojom::RelationshipKind::kDescribes;
    case EdgeType::kControls:
      return mojom::RelationshipKind::kControls;
    case EdgeType::kOwns:
      return mojom::RelationshipKind::kOwns;
    case EdgeType::kRowHeaderFor:
      return mojom::RelationshipKind::kRowHeaderFor;
    case EdgeType::kColumnHeaderFor:
      return mojom::RelationshipKind::kColumnHeaderFor;
    case EdgeType::kSameEntityAs:
      return mojom::RelationshipKind::kSameEntityAs;
    case EdgeType::kSourceFor:
      return mojom::RelationshipKind::kSourceFor;
  }
}

mojom::ValueKind ToMojom(ValueKind kind) {
  switch (kind) {
    case ValueKind::kText:
      return mojom::ValueKind::kText;
    case ValueKind::kNumber:
      return mojom::ValueKind::kNumber;
    case ValueKind::kDate:
      return mojom::ValueKind::kDate;
    case ValueKind::kSelection:
      return mojom::ValueKind::kSelection;
    case ValueKind::kToggle:
      return mojom::ValueKind::kToggle;
    case ValueKind::kPrice:
      return mojom::ValueKind::kPrice;
    case ValueKind::kRating:
      return mojom::ValueKind::kRating;
    case ValueKind::kAvailability:
      return mojom::ValueKind::kAvailability;
    case ValueKind::kSecretWithheld:
      return mojom::ValueKind::kSecretWithheld;
    case ValueKind::kUnknown:
      return mojom::ValueKind::kUnknown;
  }
}

mojom::AttributeName ToMojom(AttributeKey key) {
  switch (key) {
    case AttributeKey::kInputType:
      return mojom::AttributeName::kInputType;
    case AttributeKey::kAutocompleteToken:
      return mojom::AttributeName::kAutocompleteToken;
    case AttributeKey::kHeadingLevel:
      return mojom::AttributeName::kHeadingLevel;
    case AttributeKey::kListPosition:
      return mojom::AttributeName::kListPosition;
    case AttributeKey::kListSize:
      return mojom::AttributeName::kListSize;
    case AttributeKey::kTableRowIndex:
      return mojom::AttributeName::kTableRowIndex;
    case AttributeKey::kTableColumnIndex:
      return mojom::AttributeName::kTableColumnIndex;
    case AttributeKey::kLanguage:
      return mojom::AttributeName::kLanguage;
    case AttributeKey::kLinkRelation:
      return mojom::AttributeName::kLinkRelation;
    case AttributeKey::kMediaAltSource:
      return mojom::AttributeName::kMediaAltSource;
    case AttributeKey::kCurrencyCode:
      return mojom::AttributeName::kCurrencyCode;
    case AttributeKey::kRatingScaleMax:
      return mojom::AttributeName::kRatingScaleMax;
    case AttributeKey::kPlaceholderLabel:
      return mojom::AttributeName::kPlaceholderLabel;
  }
}

}  // namespace taffy::wire
