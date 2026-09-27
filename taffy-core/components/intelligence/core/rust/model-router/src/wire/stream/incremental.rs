// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Incremental, route-blind decoding of model response bytes.
//!
//! The public seam accepts arbitrary byte chunks and emits only visible text.
//! Framing, provider vocabulary, provider-minted call identities, raw stop
//! words, and raw frames stay inside the decoder. `finish` yields exactly one
//! typed terminal reading.

use crate::catalog::WireApi;
use crate::json::{self, JsonValue};
use crate::request::{OverflowKind, RecoverableSignal, TerminalResult};
use crate::wire::managed_stream::{
    ManagedFold, ManagedReading, ManagedReplyDefect, MAX_MANAGED_STREAM_BYTES,
};
use crate::wire::reply::ReplyContext;

use super::{apply_frame_emitting, FoldedReply, StreamDefect, StreamFold};

#[path = "incremental/adapters.rs"]
mod adapters;
#[path = "incremental/framing.rs"]
mod framing;
#[path = "incremental/reading.rs"]
mod reading;

use self::framing::{FrameDecoder, FramingDefect};
use self::reading::{canceled, sanitize};

pub(crate) use self::adapters::fold_managed;

pub(super) fn fold_direct(
    api: WireApi,
    body: &str,
    context: &ReplyContext,
) -> Result<FoldedReply, StreamDefect> {
    adapters::fold_direct(api, body, context)
}

/// Maximum bytes accepted for one direct-provider stream.
///
/// Every browser completion is bounded more tightly at its effect seam. This
/// independent sandbox bound allows at most two largest legal JSON frames.
pub const MAX_DIRECT_STREAM_BYTES: usize = 2 * json::MAX_INPUT_BYTES;

/// A reconstructed tool request with provider-minted identity removed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelStreamToolCall {
    /// The model-authored tool name.
    pub name: String,
    /// One decoded argument object.
    pub arguments: JsonValue,
}

/// The single typed terminal reading emitted by an incremental decoder.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelStreamReading {
    /// Number of visible text deltas already emitted to the caller.
    pub text_segments: u32,
    /// Complete tool calls, with no provider identity or raw fragments.
    pub tool_calls: Vec<ModelStreamToolCall>,
    /// Route-blind stop, usage, overflow, and error taxonomy.
    pub result: TerminalResult,
    /// How overflow was detected, when it was.
    pub overflow: Option<OverflowKind>,
    /// Typed recovery advice for a direct response, when one exists.
    pub recovery: Option<RecoverableSignal>,
}

/// Why incremental response bytes could not be decoded.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelStreamDefect {
    /// The route's complete-stream byte ceiling was crossed.
    InputTooLarge,
    /// One unfinished line or decoded frame crossed the JSON ceiling.
    FrameTooLarge,
    /// A completed frame was not UTF-8.
    InvalidUtf8,
    /// The transport framing was incomplete or malformed.
    MalformedFraming,
    /// A direct-provider frame or fold was invalid.
    Direct(StreamDefect),
    /// A canonical managed frame or fold was invalid.
    Managed(ManagedReplyDefect),
    /// `finish`, `cancel`, or a prior defect already closed this decoder.
    AlreadyFinished,
}

impl ModelStreamDefect {
    /// A short, compiled-in name for a diagnostic line, as
    /// [`StreamDefect::label`] gives for a direct provider's frames.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::InputTooLarge => "input_too_large",
            Self::FrameTooLarge => "frame_too_large",
            Self::InvalidUtf8 => "invalid_utf8",
            Self::MalformedFraming => "malformed_framing",
            Self::Direct(defect) => defect.label(),
            Self::Managed(_) => "managed_frame",
            Self::AlreadyFinished => "already_finished",
        }
    }
}

/// Incremental model-response decoder independent of the network transport.
///
/// `push` may be called with any chunking, including a chunk ending in the
/// middle of a UTF-8 scalar, line delimiter, JSON string, or tool-argument
/// fragment. Its callback receives only provider-normalized visible text and
/// must consume the borrowed delta before returning. `finish` flushes a legal
/// final direct frame and returns the only terminal reading. Managed NDJSON
/// deliberately requires its canonical final LF.
pub struct ModelStreamDecoder {
    protocol: Option<ProtocolFold>,
    frames: FrameDecoder,
    context: ReplyContext,
    finished: bool,
}

impl core::fmt::Debug for ModelStreamDecoder {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ModelStreamDecoder")
            .field("terminal_ready", &self.terminal_ready())
            .field("finished", &self.finished)
            .finish_non_exhaustive()
    }
}

#[derive(Debug)]
enum ProtocolFold {
    Direct {
        api: WireApi,
        fold: StreamFold,
    },
    Managed {
        expected_request_id: String,
        fold: ManagedFold,
    },
}

enum RawReading {
    Direct(FoldedReply),
    Managed(ManagedReading),
}

impl ModelStreamDecoder {
    /// Starts a decoder for one of the four direct wire families.
    pub fn direct(api: WireApi, context: ReplyContext) -> Self {
        Self {
            protocol: Some(ProtocolFold::Direct {
                api,
                fold: StreamFold::new(),
            }),
            frames: FrameDecoder::direct(MAX_DIRECT_STREAM_BYTES),
            context,
            finished: false,
        }
    }

    /// Starts a strict schema-3 canonical managed decoder.
    pub fn managed(expected_request_id: &str, context: ReplyContext) -> Self {
        Self {
            protocol: Some(ProtocolFold::Managed {
                expected_request_id: expected_request_id.to_owned(),
                fold: ManagedFold::default(),
            }),
            frames: FrameDecoder::managed(MAX_MANAGED_STREAM_BYTES),
            context,
            finished: false,
        }
    }

    /// Applies another arbitrary byte chunk and emits its visible text deltas.
    pub fn push(
        &mut self,
        chunk: &[u8],
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<(), ModelStreamDefect> {
        self.ensure_active()?;
        let frames = self.frames.push(chunk).map_err(map_framing);
        let frames = match frames {
            Ok(frames) => frames,
            Err(defect) => {
                self.finished = true;
                return Err(defect);
            }
        };
        for frame in frames {
            if let Err(defect) = self.apply_frame(&frame, emit_text) {
                self.finished = true;
                return Err(defect);
            }
        }
        Ok(())
    }

    /// Whether a typed terminal and all mandatory usage are already present.
    ///
    /// The caller may still push trailing usage frames to merge cumulative
    /// counters or to validate that no second terminal follows.
    pub fn terminal_ready(&self) -> bool {
        if self.finished {
            return false;
        }
        self.protocol.as_ref().is_some_and(ProtocolFold::ready)
    }

    /// Ends a complete transport, emits text from a legal final direct frame,
    /// and returns the one sanitized terminal reading.
    pub fn finish(
        &mut self,
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<ModelStreamReading, ModelStreamDefect> {
        self.finish_raw(emit_text).map(sanitize)
    }

    /// Ends an interrupted transport with a typed cancellation terminal.
    ///
    /// If a complete terminal was already decoded, that terminal wins.
    pub fn cancel(&mut self) -> Result<ModelStreamReading, ModelStreamDefect> {
        self.ensure_active()?;
        if self.terminal_ready() {
            return self.finish(&mut |_| {});
        }
        let text_segments = self
            .protocol
            .as_ref()
            .map_or(0, ProtocolFold::text_segments);
        self.finished = true;
        self.protocol = None;
        Ok(canceled(self.context, text_segments))
    }

    fn ensure_active(&self) -> Result<(), ModelStreamDefect> {
        if self.finished || self.protocol.is_none() {
            Err(ModelStreamDefect::AlreadyFinished)
        } else {
            Ok(())
        }
    }

    fn apply_frame(
        &mut self,
        frame: &str,
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<(), ModelStreamDefect> {
        match self
            .protocol
            .as_mut()
            .ok_or(ModelStreamDefect::AlreadyFinished)?
        {
            ProtocolFold::Direct { api, fold } => {
                apply_frame_emitting(*api, frame, fold, emit_text)
                    .map_err(ModelStreamDefect::Direct)
            }
            ProtocolFold::Managed {
                expected_request_id,
                fold,
            } => {
                let document = json::parse_provider(frame)
                    .map_err(|_| ModelStreamDefect::Managed(ManagedReplyDefect::MalformedFrame))?;
                fold.apply_emitting(&document, expected_request_id, emit_text)
                    .map_err(ModelStreamDefect::Managed)
            }
        }
    }

    fn finish_raw(
        &mut self,
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<RawReading, ModelStreamDefect> {
        self.ensure_active()?;
        let frames = match self.frames.finish().map_err(map_framing) {
            Ok(frames) => frames,
            Err(defect) => {
                self.finished = true;
                return Err(defect);
            }
        };
        for frame in frames {
            if let Err(defect) = self.apply_frame(&frame, emit_text) {
                self.finished = true;
                return Err(defect);
            }
        }
        self.finished = true;
        let protocol = self
            .protocol
            .take()
            .ok_or(ModelStreamDefect::AlreadyFinished)?;
        match protocol {
            ProtocolFold::Direct { fold, .. } => fold
                .finish(&self.context)
                .map(RawReading::Direct)
                .map_err(ModelStreamDefect::Direct),
            ProtocolFold::Managed { fold, .. } => fold
                .finish(&self.context)
                .map(RawReading::Managed)
                .map_err(ModelStreamDefect::Managed),
        }
    }
}

impl ProtocolFold {
    fn ready(&self) -> bool {
        match self {
            Self::Direct { fold, .. } => fold.can_finish(),
            Self::Managed { fold, .. } => fold.is_terminated(),
        }
    }

    fn text_segments(&self) -> u32 {
        match self {
            Self::Direct { fold, .. } => fold.text_segments(),
            Self::Managed { fold, .. } => fold.text_segments(),
        }
    }
}

const fn map_framing(defect: FramingDefect) -> ModelStreamDefect {
    match defect {
        FramingDefect::InputTooLarge => ModelStreamDefect::InputTooLarge,
        FramingDefect::FrameTooLarge => ModelStreamDefect::FrameTooLarge,
        FramingDefect::InvalidUtf8 => ModelStreamDefect::InvalidUtf8,
        FramingDefect::MalformedFrame => ModelStreamDefect::MalformedFraming,
    }
}
