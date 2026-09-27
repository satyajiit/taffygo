// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict generated observation-result decoding into content-free evidence.

mod graph;
mod media;

pub use media::decode_media_observation;

use bip_types::identity::{FrameId, PageEpoch, TabId};
use bip_types::{ProtocolVersion, Sensitivity};
use core_service_types as wire;
use task_engine::{
    ObservationCompleteness, PageObservationEvidence, MAX_OBSERVATION_IDENTIFIER_BYTES,
    MAX_OBSERVATION_ORIGIN_BYTES, MAX_OBSERVATION_SCHEMA_VERSION_BYTES,
};

/// Why an observation terminal could not become reducer evidence.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ObservationWireError {
    /// The endpoint did not return a usable terminal graph.
    UnsupportedStatus,
    /// Typed envelope facts were absent, contradictory, or outside bounds.
    InvalidEnvelope,
    /// The BIP schema version was malformed or incompatible.
    UnsupportedVersion,
    /// The payload did not use the reviewed generated BIP graph framing.
    InvalidEncoding,
    /// The encoded graph exceeded the task effect's reviewed limits.
    PayloadLimit,
    /// The graph body was malformed or used an unknown closed value.
    MalformedGraph,
    /// Envelope counts did not match the strictly decoded graph.
    GraphMismatch,
    /// Media facts or their opaque attachment binding were malformed.
    InvalidMedia,
}

impl ObservationWireError {
    /// Content-free diagnostic spelling.
    pub const fn label(self) -> &'static str {
        match self {
            Self::UnsupportedStatus => "unsupported_status",
            Self::InvalidEnvelope => "invalid_envelope",
            Self::UnsupportedVersion => "unsupported_version",
            Self::InvalidEncoding => "invalid_encoding",
            Self::PayloadLimit => "payload_limit",
            Self::MalformedGraph => "malformed_graph",
            Self::GraphMismatch => "graph_mismatch",
            Self::InvalidMedia => "invalid_media",
        }
    }
}

/// One observation completion, borrowed rather than owned.
///
/// The graph payload reaches the process byte cap, so nothing on the path
/// between the bridge and this decoder is allowed to hold a second copy of it:
/// the bytes are validated, decoded into structural facts, and dropped without
/// ever being cloned. Every other field is borrowed for the same reason it is
/// read — once.
#[derive(Clone, Copy, Debug)]
pub struct ObservationEffectView<'a> {
    pub status: wire::BipObservationStatus,
    pub schema_version: &'a str,
    pub tab_id: &'a str,
    pub frame_id: &'a str,
    pub page_epoch: &'a str,
    pub graph_revision: u64,
    pub origin: &'a str,
    pub private_profile: bool,
    pub node_count: u32,
    pub total_bytes: u32,
    pub truncated: bool,
    pub may_change_answer: bool,
    pub redacted_field_count: u32,
    pub suppressed_secret_value_count: u32,
    pub sensitive_zone_count: u32,
    pub policy_filtered_frame_count: u32,
    pub highest_sensitivity: wire::BipSensitivity,
    pub graph_encoding: wire::BipGraphEncoding,
    pub graph_payload: &'a [u8],
}

impl<'a> From<&'a wire::ObservationEffectResult> for ObservationEffectView<'a> {
    fn from(result: &'a wire::ObservationEffectResult) -> Self {
        Self {
            status: result.status,
            schema_version: result.schema_version.as_str(),
            tab_id: result.tab_id.as_str(),
            frame_id: result.frame_id.as_str(),
            page_epoch: result.page_epoch.as_str(),
            graph_revision: result.graph_revision,
            origin: result.origin.as_str(),
            private_profile: result.private_profile,
            node_count: result.node_count,
            total_bytes: result.total_bytes,
            truncated: result.truncated,
            may_change_answer: result.may_change_answer,
            redacted_field_count: result.redacted_field_count,
            suppressed_secret_value_count: result.suppressed_secret_value_count,
            sensitive_zone_count: result.sensitive_zone_count,
            policy_filtered_frame_count: result.policy_filtered_frame_count,
            highest_sensitivity: result.highest_sensitivity,
            graph_encoding: result.graph_encoding,
            graph_payload: result.graph_payload.as_slice(),
        }
    }
}

/// Decodes one exact BIP observation completion into durable structural facts.
///
/// Raw graph bytes, node identifiers, and redacted node names are consumed
/// only while validating this call and are never returned or persisted.
pub fn decode_page_observation_evidence(
    service_generation: u64,
    result: ObservationEffectView<'_>,
) -> Result<PageObservationEvidence, ObservationWireError> {
    decode_page_observation(service_generation, result, None)
}

/// The same decode, additionally keeping what a projection needs.
///
/// The evidence half is durable and content-free; the arena half is neither,
/// and is dropped when the turn that asked for the page ends
/// ([`crate::context::PageArena`]). They come from one pass because they are
/// one payload: a second pass could reach a different verdict from the same
/// bytes, and then there would be two answers to which page was observed.
pub fn decode_page_observation(
    service_generation: u64,
    result: ObservationEffectView<'_>,
    arena: Option<&mut crate::context::PageArena>,
) -> Result<PageObservationEvidence, ObservationWireError> {
    let completeness = match result.status {
        wire::BipObservationStatus::Ok => ObservationCompleteness::Complete,
        // A page whose structured data disagrees with its own text is still a
        // reading: decision 0207 made the browser deliver `Conflicted` as one,
        // with its status kept so a consumer knows the graph carries competing
        // candidates. This decoder was never told, so the first such page on
        // a phone had its completion refused as `unsupported_status`, the
        // browser synthesized an unknown outcome for a read, and the errand
        // was handed to the person. The caveat is exactly what `Incomplete`
        // says — a useful graph whose answer may be affected — and it keeps
        // the reading from supporting a complete result (decision 0234).
        wire::BipObservationStatus::Incomplete | wire::BipObservationStatus::Conflicted => {
            ObservationCompleteness::Incomplete
        }
        _ => return Err(ObservationWireError::UnsupportedStatus),
    };
    validate_envelope(service_generation, result, completeness)?;
    let graph = graph::decode(
        result.graph_payload,
        result.schema_version,
        result.frame_id,
        arena,
    )
    .map_err(|_| ObservationWireError::MalformedGraph)?;
    if graph.node_count != result.node_count {
        return Err(ObservationWireError::GraphMismatch);
    }
    let evidence = PageObservationEvidence {
        service_generation,
        schema_version: result.schema_version.to_owned(),
        tab_id: TabId(result.tab_id.to_owned()),
        frame_id: FrameId(result.frame_id.to_owned()),
        page_epoch: PageEpoch(result.page_epoch.to_owned()),
        graph_revision: result.graph_revision,
        normalized_origin: result.origin.to_owned(),
        private_profile: result.private_profile,
        completeness,
        graph,
        total_bytes: result.total_bytes,
        truncated: result.truncated,
        may_change_answer: result.may_change_answer,
        redacted_field_count: result.redacted_field_count,
        suppressed_secret_value_count: result.suppressed_secret_value_count,
        sensitive_zone_count: result.sensitive_zone_count,
        policy_filtered_frame_count: result.policy_filtered_frame_count,
        highest_sensitivity: sensitivity(result.highest_sensitivity),
    };
    evidence
        .is_valid()
        .then_some(evidence)
        .ok_or(ObservationWireError::InvalidEnvelope)
}

fn validate_envelope(
    service_generation: u64,
    result: ObservationEffectView<'_>,
    completeness: ObservationCompleteness,
) -> Result<(), ObservationWireError> {
    let payload_len = u32::try_from(result.graph_payload.len())
        .map_err(|_| ObservationWireError::PayloadLimit)?;
    if service_generation == 0
        || !bounded_non_empty(result.tab_id, MAX_OBSERVATION_IDENTIFIER_BYTES.min(128))
        || !bounded_non_empty(result.frame_id, MAX_OBSERVATION_IDENTIFIER_BYTES.min(128))
        || !bounded_non_empty(result.page_epoch, MAX_OBSERVATION_IDENTIFIER_BYTES.min(128))
        || !bounded_non_empty(result.origin, MAX_OBSERVATION_ORIGIN_BYTES.min(512))
        || result.graph_revision == 0
        || result.total_bytes != payload_len
    {
        return Err(ObservationWireError::InvalidEnvelope);
    }
    if result.graph_encoding != wire::BipGraphEncoding::BipContract {
        return Err(ObservationWireError::InvalidEncoding);
    }
    if result.graph_payload.is_empty()
        || result.graph_payload.len() > wire::MAX_TASK_OBSERVATION_TOTAL_BYTES
        || usize::try_from(result.node_count)
            .map_or(true, |count| count > wire::MAX_TASK_OBSERVATION_NODES)
    {
        return Err(ObservationWireError::PayloadLimit);
    }
    validate_version(result.schema_version)?;
    // A complete observation may not also say it was truncated or that its
    // answer may change: those flags are exactly what makes one incomplete, and
    // an envelope claiming both is contradicting itself.
    //
    // The converse does not hold, and assuming it did ended an errand on a
    // phone that had just read the site it was sent to. Incompleteness has two
    // independent sources. A budget that cut the walk short sets `truncated`;
    // an adapter that could not fully report sets nothing at all, because
    // `observation_result_builder.cc` combines the adapter code into the result
    // code before the truncation flag is consulted. A portal whose metadata
    // adapter came back conflicted therefore arrived as INCOMPLETE with
    // `truncated` clear, this clause called that envelope invalid, the
    // completion was refused, and the task ended holding nine hundred and
    // ninety-eight nodes it had read perfectly well (decision 0171).
    if matches!(completeness, ObservationCompleteness::Complete)
        && (result.truncated || result.may_change_answer)
    {
        return Err(ObservationWireError::InvalidEnvelope);
    }
    Ok(())
}

fn validate_version(value: &str) -> Result<(), ObservationWireError> {
    if !bounded_non_empty(value, MAX_OBSERVATION_SCHEMA_VERSION_BYTES) {
        return Err(ObservationWireError::UnsupportedVersion);
    }
    let parsed =
        ProtocolVersion::parse(value).map_err(|_| ObservationWireError::UnsupportedVersion)?;
    let current =
        ProtocolVersion::current().map_err(|_| ObservationWireError::UnsupportedVersion)?;
    if parsed.to_string() != value || !current.is_compatible_with(parsed) {
        return Err(ObservationWireError::UnsupportedVersion);
    }
    Ok(())
}

fn bounded_non_empty(value: &str, maximum: usize) -> bool {
    !value.is_empty() && value.len() <= maximum
}

const fn sensitivity(value: wire::BipSensitivity) -> Sensitivity {
    match value {
        wire::BipSensitivity::NotSensitive => Sensitivity::NotSensitive,
        wire::BipSensitivity::Personal => Sensitivity::Personal,
        wire::BipSensitivity::Account => Sensitivity::Account,
        wire::BipSensitivity::Payment => Sensitivity::Payment,
        wire::BipSensitivity::Identity => Sensitivity::Identity,
        wire::BipSensitivity::Health => Sensitivity::Health,
        wire::BipSensitivity::Financial => Sensitivity::Financial,
        wire::BipSensitivity::Legal => Sensitivity::Legal,
        wire::BipSensitivity::PrivateCommunication => Sensitivity::PrivateCommunication,
        wire::BipSensitivity::Administration => Sensitivity::Administration,
        wire::BipSensitivity::Credential => Sensitivity::Credential,
        wire::BipSensitivity::UnknownSensitive => Sensitivity::UnknownSensitive,
        wire::BipSensitivity::OneTimeCode => Sensitivity::OneTimeCode,
        wire::BipSensitivity::ChallengeResponse => Sensitivity::ChallengeResponse,
    }
}

#[cfg(test)]
mod tests;
