// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded, typed projection of one browser-verified media probe.

/// Maximum media-probe material one result may add to a model request.
///
/// The five varying values are bounded integers and the labels are compiled
/// in. Keeping a separate byte ceiling still makes the transcript cost an
/// invariant of this type rather than an assumption in its caller.
pub const MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES: usize = 256;

const MAX_MEDIA_PROBE_TRANSCRIPT_PIECE_BYTES: usize = 48;

/// Why a worker reading cannot become model-visible probe facts.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaProbeResultError {
    /// One numeric field exceeded the browser-reviewed probe envelope.
    OutOfBounds,
    /// A successful probe must name at least one audio or video stream.
    NoStreams,
    /// Dimensions without a video stream describe no stream the probe read.
    DimensionsWithoutVideo,
}

/// Browser-verified scalar facts from one `media.probe` call.
///
/// No container string, path, worker byte array, or source identifier can be
/// represented here. Provenance is supplied by the exact action/call binding
/// that installs this value into turn residency after the durable outcome
/// commits.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MediaProbeTranscriptOutcome {
    duration_ms: u64,
    audio_streams: u32,
    video_streams: u32,
    width_px: u32,
    height_px: u32,
    pieces: [String; 6],
}

impl MediaProbeTranscriptOutcome {
    pub const MAX_DURATION_MS: u64 = 600_000;
    pub const MAX_STREAMS: u32 = 64;
    pub const MAX_WIDTH_PX: u32 = 1_920;
    pub const MAX_HEIGHT_PX: u32 = 1_080;

    /// Validates and renders one fixed-size reading.
    pub fn new(
        duration_ms: u64,
        audio_streams: u32,
        video_streams: u32,
        width_px: u32,
        height_px: u32,
    ) -> Result<Self, MediaProbeResultError> {
        if duration_ms > Self::MAX_DURATION_MS
            || audio_streams > Self::MAX_STREAMS
            || video_streams > Self::MAX_STREAMS
            || width_px > Self::MAX_WIDTH_PX
            || height_px > Self::MAX_HEIGHT_PX
        {
            return Err(MediaProbeResultError::OutOfBounds);
        }
        if audio_streams == 0 && video_streams == 0 {
            return Err(MediaProbeResultError::NoStreams);
        }
        if video_streams == 0 && (width_px != 0 || height_px != 0) {
            return Err(MediaProbeResultError::DimensionsWithoutVideo);
        }
        let pieces = [
            "Verified media probe facts:".to_owned(),
            format!("duration_ms: {duration_ms}"),
            format!("audio_streams: {audio_streams}"),
            format!("video_streams: {video_streams}"),
            format!("width_px: {width_px}"),
            format!("height_px: {height_px}"),
        ];
        let rendered_bytes = pieces
            .iter()
            .map(String::len)
            .fold(0usize, usize::saturating_add);
        if rendered_bytes > MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES
            || pieces
                .iter()
                .any(|piece| piece.len() > MAX_MEDIA_PROBE_TRANSCRIPT_PIECE_BYTES)
        {
            return Err(MediaProbeResultError::OutOfBounds);
        }
        Ok(Self {
            duration_ms,
            audio_streams,
            video_streams,
            width_px,
            height_px,
            pieces,
        })
    }

    pub const fn tool_name(&self) -> &'static str {
        "media.probe"
    }

    pub const fn duration_ms(&self) -> u64 {
        self.duration_ms
    }

    pub const fn audio_streams(&self) -> u32 {
        self.audio_streams
    }

    pub const fn video_streams(&self) -> u32 {
        self.video_streams
    }

    pub const fn width_px(&self) -> u32 {
        self.width_px
    }

    pub const fn height_px(&self) -> u32 {
        self.height_px
    }

    pub fn result_pieces(&self) -> &[String] {
        &self.pieces
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_rendered_field_and_the_whole_result_are_bounded() {
        let outcome = MediaProbeTranscriptOutcome::new(600_000, 64, 64, 1_920, 1_080)
            .unwrap_or_else(|_| unreachable!("reviewed maxima are valid"));
        let rendered = outcome.result_pieces().concat();

        assert!(rendered.contains("duration_ms: 600000"));
        assert!(rendered.contains("audio_streams: 64"));
        assert!(rendered.contains("video_streams: 64"));
        assert!(rendered.contains("width_px: 1920"));
        assert!(rendered.contains("height_px: 1080"));
        assert!(rendered.len() <= MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES);
        assert!(outcome
            .result_pieces()
            .iter()
            .all(|piece| piece.len() <= MAX_MEDIA_PROBE_TRANSCRIPT_PIECE_BYTES));
    }

    #[test]
    fn invalid_or_invented_readings_are_refused() {
        assert_eq!(
            MediaProbeTranscriptOutcome::new(600_001, 1, 0, 0, 0),
            Err(MediaProbeResultError::OutOfBounds)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 65, 0, 0, 0),
            Err(MediaProbeResultError::OutOfBounds)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 0, 65, 0, 0),
            Err(MediaProbeResultError::OutOfBounds)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 0, 1, 1_921, 1_080),
            Err(MediaProbeResultError::OutOfBounds)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 0, 1, 1_920, 1_081),
            Err(MediaProbeResultError::OutOfBounds)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 0, 0, 0, 0),
            Err(MediaProbeResultError::NoStreams)
        );
        assert_eq!(
            MediaProbeTranscriptOutcome::new(1, 1, 0, 1, 0),
            Err(MediaProbeResultError::DimensionsWithoutVideo)
        );
    }
}
