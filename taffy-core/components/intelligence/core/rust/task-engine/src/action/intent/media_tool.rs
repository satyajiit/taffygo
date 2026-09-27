// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed intent for one descriptor-backed media operation.

use bip_types::identity::{SemanticNodeId, TabId};

use crate::artifact::ArtifactKind;
use crate::task::BrowserSessionId;
use crate::tool::IdempotencyClass;

/// Media operations exposed by the compiled registry.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum MediaOperation {
    Probe,
    ExtractAudio,
    SampleFrames,
    Transcode,
}

impl MediaOperation {
    pub const fn from_tool_name(value: &str) -> Option<Self> {
        match value.as_bytes() {
            b"media.probe" => Some(Self::Probe),
            b"media.audio.extract" => Some(Self::ExtractAudio),
            b"media.frames.sample" => Some(Self::SampleFrames),
            b"media.transcode" => Some(Self::Transcode),
            _ => None,
        }
    }

    pub const fn tool_name(self) -> &'static str {
        match self {
            Self::Probe => "media.probe",
            Self::ExtractAudio => "media.audio.extract",
            Self::SampleFrames => "media.frames.sample",
            Self::Transcode => "media.transcode",
        }
    }

    pub const fn artifact_kind(self) -> Option<ArtifactKind> {
        match self {
            Self::Probe => None,
            Self::ExtractAudio | Self::Transcode => Some(ArtifactKind::WaveAudio),
            Self::SampleFrames => Some(ArtifactKind::FrameArchive),
        }
    }

    pub const fn wire_tag(self) -> u8 {
        match self {
            Self::Probe => 0,
            Self::ExtractAudio => 1,
            Self::SampleFrames => 2,
            Self::Transcode => 3,
        }
    }

    pub(crate) const fn from_wire(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::Probe),
            1 => Some(Self::ExtractAudio),
            2 => Some(Self::SampleFrames),
            3 => Some(Self::Transcode),
            _ => None,
        }
    }
}

/// One media operation over a browser-observed completed download.
///
/// `source_id` is Chromium's opaque download identity resolved from a short
/// model handle. It is not a path and grants no authority: the browser must
/// still find the same admitted descriptor under this task and generation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaToolIntent {
    pub tool_name: String,
    pub operation: MediaOperation,
    pub source_id: String,
    pub source_browser_session_id: BrowserSessionId,
    /// Exact completed-download size witnessed by the browser listing.
    pub source_bytes: u64,
    pub tab: TabId,
    pub node: Option<SemanticNodeId>,
    /// Nonzero only for [`MediaOperation::SampleFrames`].
    pub max_frames: u32,
    pub idempotency: IdempotencyClass,
}
