// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The payload a caller supplies, and the field vocabulary it must use.
//!
//! A payload is a bounded list of typed fields, never a free-form map. Both
//! halves matter:
//!
//! - the **name** comes from a closed list, so a serializer decides per field
//!   rather than per event, and a field nobody has written a policy for is
//!   dropped rather than emitted;
//! - the **value kind** is declared by the name, so a caller that puts page
//!   text where an enumerated name belongs has its field dropped rather than
//!   serialized. A recorder that trusted the caller's own labelling would be no
//!   control at all.
//!
//! Names that are never eligible for a record are present in the enumeration on
//! purpose — a page title, a prompt, a model output, a file name. They exist so
//! that refusing them is a table a test can walk rather than an absence nobody
//! notices, exactly as the denied action classes do in `policy-engine`.

use core::fmt;

use bip_types::action::{ActionResultCode, VerifierKind};
use bip_types::snapshot::Sensitivity;

/// The shape of a field's value.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ValueKind {
    /// An opaque domain identifier.
    Identifier,
    /// A compiled-in enumerated name.
    Enumerated,
    /// A non-negative count or bucket.
    Count,
    /// A boolean decision fact.
    Flag,
    /// A URL. Only its normalized origin can ever be recorded.
    Url,
    /// Caller-supplied text.
    Text,
}

impl ValueKind {
    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Identifier => "identifier",
            Self::Enumerated => "enumerated",
            Self::Count => "count",
            Self::Flag => "flag",
            Self::Url => "url",
            Self::Text => "text",
        }
    }
}

/// A field of an event payload.
///
/// The list is closed. Adding a field means adding it here and deciding, in
/// [`crate::redaction`], what each serializer does with it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum FieldName {
    /// The task the event belongs to.
    TaskId,
    /// The action being decided.
    ActionId,
    /// The capability that authorized it.
    CapabilityId,
    /// The actor lease it was issued under.
    ActorLeaseId,
    /// The approval receipt.
    ApprovalId,
    /// The observed node.
    NodeId,
    /// The document instance.
    PageEpoch,
    /// The graph revision an observation was taken at.
    GraphRevision,
    /// The class of effect.
    ActionClass,
    /// The protocol result code.
    ResultCode,
    /// The policy decision reason.
    DecisionReason,
    /// The step of the pre-dispatch sequence a refusal stopped at.
    DecisionStep,
    /// The control mode in force.
    ControlMode,
    /// The approval decision.
    ApprovalDecision,
    /// What verified the effect.
    VerifierKind,
    /// The sensitivity classification that applied.
    Sensitivity,
    /// The data class that crossed a boundary.
    DataClass,
    /// The provider route selected.
    ProviderRoute,
    /// The model identifier.
    ModelId,
    /// The policy bundle version.
    PolicyVersion,
    /// The origin an action was bound to.
    OriginNormalized,
    /// The destination a navigation was headed for.
    DestinationUrl,
    /// How many sources were in scope.
    SourceCount,
    /// A byte bucket.
    ByteCount,
    /// A latency bucket.
    LatencyBucket,
    /// Whether a budget was exceeded.
    BudgetExceeded,
    /// Whether any content value was retained.
    ContentValuesRetained,
    /// A bounded machine-readable error code.
    ErrorCode,
    /// The sentence shown to a person.
    UserVisibleSummary,
    /// A page title.
    PageTitle,
    /// A prompt sent to a model.
    PromptText,
    /// A model's output.
    ModelOutput,
    /// A file name.
    FileName,
    /// Text selected by the user.
    SelectedText,
}

impl FieldName {
    /// Every field name, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TaskId,
        Self::ActionId,
        Self::CapabilityId,
        Self::ActorLeaseId,
        Self::ApprovalId,
        Self::NodeId,
        Self::PageEpoch,
        Self::GraphRevision,
        Self::ActionClass,
        Self::ResultCode,
        Self::DecisionReason,
        Self::DecisionStep,
        Self::ControlMode,
        Self::ApprovalDecision,
        Self::VerifierKind,
        Self::Sensitivity,
        Self::DataClass,
        Self::ProviderRoute,
        Self::ModelId,
        Self::PolicyVersion,
        Self::OriginNormalized,
        Self::DestinationUrl,
        Self::SourceCount,
        Self::ByteCount,
        Self::LatencyBucket,
        Self::BudgetExceeded,
        Self::ContentValuesRetained,
        Self::ErrorCode,
        Self::UserVisibleSummary,
        Self::PageTitle,
        Self::PromptText,
        Self::ModelOutput,
        Self::FileName,
        Self::SelectedText,
    ];

    /// The compiled-in name recorded in a serialized field.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TaskId => "task_id",
            Self::ActionId => "action_id",
            Self::CapabilityId => "capability_id",
            Self::ActorLeaseId => "actor_lease_id",
            Self::ApprovalId => "approval_id",
            Self::NodeId => "node_id",
            Self::PageEpoch => "page_epoch",
            Self::GraphRevision => "graph_revision",
            Self::ActionClass => "action_class",
            Self::ResultCode => "result_code",
            Self::DecisionReason => "decision_reason",
            Self::DecisionStep => "decision_step",
            Self::ControlMode => "control_mode",
            Self::ApprovalDecision => "approval_decision",
            Self::VerifierKind => "verifier_kind",
            Self::Sensitivity => "sensitivity",
            Self::DataClass => "data_class",
            Self::ProviderRoute => "provider_route",
            Self::ModelId => "model_id",
            Self::PolicyVersion => "policy_version",
            Self::OriginNormalized => "origin",
            Self::DestinationUrl => "destination",
            Self::SourceCount => "source_count",
            Self::ByteCount => "byte_count",
            Self::LatencyBucket => "latency_bucket",
            Self::BudgetExceeded => "budget_exceeded",
            Self::ContentValuesRetained => "content_values_retained",
            Self::ErrorCode => "error_code",
            Self::UserVisibleSummary => "user_visible_summary",
            Self::PageTitle => "page_title",
            Self::PromptText => "prompt_text",
            Self::ModelOutput => "model_output",
            Self::FileName => "file_name",
            Self::SelectedText => "selected_text",
        }
    }

    /// The value kind this field must carry.
    ///
    /// A field whose value is of another kind is dropped by both serializers,
    /// whatever the caller believed it was recording.
    pub const fn expected_kind(self) -> ValueKind {
        match self {
            Self::TaskId
            | Self::ActionId
            | Self::CapabilityId
            | Self::ActorLeaseId
            | Self::ApprovalId
            | Self::NodeId
            | Self::PageEpoch
            | Self::ModelId => ValueKind::Identifier,
            Self::ActionClass
            | Self::ResultCode
            | Self::DecisionReason
            | Self::DecisionStep
            | Self::ControlMode
            | Self::ApprovalDecision
            | Self::VerifierKind
            | Self::Sensitivity
            | Self::DataClass
            | Self::ProviderRoute
            | Self::ErrorCode => ValueKind::Enumerated,
            Self::GraphRevision
            | Self::PolicyVersion
            | Self::SourceCount
            | Self::ByteCount
            | Self::LatencyBucket => ValueKind::Count,
            Self::BudgetExceeded | Self::ContentValuesRetained => ValueKind::Flag,
            Self::OriginNormalized | Self::DestinationUrl => ValueKind::Url,
            Self::UserVisibleSummary
            | Self::PageTitle
            | Self::PromptText
            | Self::ModelOutput
            | Self::FileName
            | Self::SelectedText => ValueKind::Text,
        }
    }
}

impl fmt::Display for FieldName {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.label())
    }
}

/// One field's value, as the caller supplied it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FieldValue {
    /// An opaque domain identifier.
    Identifier(String),
    /// A compiled-in enumerated name. The `'static` lifetime is the point: a
    /// value of this kind cannot have come from a page or a model.
    Enumerated(&'static str),
    /// A non-negative count or bucket.
    Count(u64),
    /// A boolean decision fact.
    Flag(bool),
    /// A URL, as observed. Only its normalized origin can ever be recorded.
    Url(String),
    /// Caller-supplied text.
    Text(String),
}

impl FieldValue {
    /// The kind of this value.
    pub const fn kind(&self) -> ValueKind {
        match self {
            Self::Identifier(_) => ValueKind::Identifier,
            Self::Enumerated(_) => ValueKind::Enumerated,
            Self::Count(_) => ValueKind::Count,
            Self::Flag(_) => ValueKind::Flag,
            Self::Url(_) => ValueKind::Url,
            Self::Text(_) => ValueKind::Text,
        }
    }
}

/// One field of a payload.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PayloadField {
    /// Which field.
    pub name: FieldName,
    /// Its value, as the caller supplied it.
    pub value: FieldValue,
}

/// The decision facts of one event.
///
/// Order is preserved so a serialized record is deterministic. A duplicate
/// field name is kept rather than merged: silently choosing between two
/// disagreeing values is the behaviour this whole crate exists to avoid.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct EventPayload {
    fields: Vec<PayloadField>,
}

impl EventPayload {
    /// An empty payload.
    pub const fn new() -> Self {
        Self { fields: Vec::new() }
    }

    /// Adds a field.
    #[must_use]
    pub fn with(mut self, name: FieldName, value: FieldValue) -> Self {
        self.fields.push(PayloadField { name, value });
        self
    }

    /// Adds an identifier field.
    #[must_use]
    pub fn with_identifier(self, name: FieldName, value: impl Into<String>) -> Self {
        self.with(name, FieldValue::Identifier(value.into()))
    }

    /// Adds an enumerated field.
    #[must_use]
    pub fn with_enumerated(self, name: FieldName, value: &'static str) -> Self {
        self.with(name, FieldValue::Enumerated(value))
    }

    /// Adds a count field.
    #[must_use]
    pub fn with_count(self, name: FieldName, value: u64) -> Self {
        self.with(name, FieldValue::Count(value))
    }

    /// Adds a flag field.
    #[must_use]
    pub fn with_flag(self, name: FieldName, value: bool) -> Self {
        self.with(name, FieldValue::Flag(value))
    }

    /// Adds a URL field. Only its normalized origin can ever be recorded.
    #[must_use]
    pub fn with_url(self, name: FieldName, value: impl Into<String>) -> Self {
        self.with(name, FieldValue::Url(value.into()))
    }

    /// Records a protocol result code.
    ///
    /// The wire name comes from the generated bindings, so an enumerated field
    /// carries a value the protocol declared rather than a string a caller
    /// composed. A code that did not decode never reaches here: an unknown wire
    /// value has no member to pass in.
    #[must_use]
    pub fn with_result_code(self, code: ActionResultCode) -> Self {
        self.with_enumerated(FieldName::ResultCode, code.wire())
    }

    /// Records what observed an effect.
    #[must_use]
    pub fn with_verifier(self, verifier: VerifierKind) -> Self {
        self.with_enumerated(FieldName::VerifierKind, verifier.wire())
    }

    /// Records the sensitivity classification that applied.
    #[must_use]
    pub fn with_sensitivity(self, sensitivity: Sensitivity) -> Self {
        self.with_enumerated(FieldName::Sensitivity, sensitivity.wire())
    }

    /// The fields, in the order they were added.
    pub fn fields(&self) -> &[PayloadField] {
        &self.fields
    }

    /// How many fields the payload holds.
    pub fn len(&self) -> usize {
        self.fields.len()
    }

    /// Whether the payload holds no fields.
    pub fn is_empty(&self) -> bool {
        self.fields.is_empty()
    }

    /// The first value recorded under `name`.
    pub fn get(&self, name: FieldName) -> Option<&FieldValue> {
        self.fields
            .iter()
            .find(|field| field.name == name)
            .map(|field| &field.value)
    }
}

#[cfg(test)]
mod tests {
    use super::{EventPayload, FieldName, FieldValue, ValueKind};
    use bip_types::action::{ActionResultCode, VerifierKind};
    use bip_types::snapshot::Sensitivity;

    #[test]
    fn a_protocol_value_is_recorded_by_its_compiled_in_wire_name() {
        let payload = EventPayload::new()
            .with_result_code(ActionResultCode::StalePageEpoch)
            .with_verifier(VerifierKind::FreshSnapshot)
            .with_sensitivity(Sensitivity::Payment);

        assert_eq!(
            payload.get(FieldName::ResultCode),
            Some(&FieldValue::Enumerated("STALE_PAGE_EPOCH"))
        );
        assert_eq!(
            payload.get(FieldName::VerifierKind),
            Some(&FieldValue::Enumerated("FRESH_SNAPSHOT"))
        );
        assert_eq!(
            payload.get(FieldName::Sensitivity),
            Some(&FieldValue::Enumerated("PAYMENT"))
        );
    }

    #[test]
    fn every_field_name_declares_the_kind_it_carries() {
        // Walking the vocabulary rather than a list: a name added without a
        // kind fails to compile, and a name added without a policy is dropped
        // by both serializers.
        for name in FieldName::ALL {
            let kind = name.expected_kind();
            let value = match kind {
                ValueKind::Identifier => FieldValue::Identifier("id_1".to_owned()),
                ValueKind::Enumerated => FieldValue::Enumerated("SOMETHING"),
                ValueKind::Count => FieldValue::Count(1),
                ValueKind::Flag => FieldValue::Flag(true),
                ValueKind::Url => FieldValue::Url("https://example.test".to_owned()),
                ValueKind::Text => FieldValue::Text("text".to_owned()),
            };
            assert_eq!(value.kind(), kind, "{}", name.label());
        }
    }

    #[test]
    fn a_duplicate_field_is_kept_rather_than_merged() {
        let payload = EventPayload::new()
            .with_count(FieldName::SourceCount, 1)
            .with_count(FieldName::SourceCount, 2);
        assert_eq!(payload.len(), 2);
        // The first one wins a lookup; both survive into the record, so a
        // reader sees the disagreement instead of a silently chosen winner.
        assert_eq!(
            payload.get(FieldName::SourceCount),
            Some(&FieldValue::Count(1))
        );
    }
}
