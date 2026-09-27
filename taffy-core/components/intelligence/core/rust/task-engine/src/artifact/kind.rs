// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which format an artifact is in, and the filename it suggests.
//!
//! The two belong together: the extension, the internal tool name, and the
//! suggested filename are all consequences of the format, and a new format
//! that forgot one of them would be a format the document picker could not
//! name.
/// Which format an artifact is in.
///
/// Markdown and CSV are rendered by this crate. The four rich formats are
/// rendered and validated by `file-engine`; keeping all six in this closed
/// identity type lets the durable task lifecycle name a file without learning
/// which encoder owns it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ArtifactKind {
    /// Markdown.
    Markdown,
    /// Comma-separated values.
    Csv,
    /// Office Open XML workbook.
    Xlsx,
    /// Portable Document Format document.
    Pdf,
    /// Office Open XML word-processing document.
    Docx,
    /// Office Open XML presentation.
    Pptx,
    /// Linear-PCM WAVE audio produced by the media worker.
    WaveAudio,
    /// ZIP archive of sampled PNG frames and its manifest.
    FrameArchive,
}

impl ArtifactKind {
    /// Every admitted format, in stable declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Markdown,
        Self::Csv,
        Self::Xlsx,
        Self::Pdf,
        Self::Docx,
        Self::Pptx,
        Self::WaveAudio,
        Self::FrameArchive,
    ];

    /// Formats rendered by the inert text artifact engine in this crate.
    pub const TEXT: &'static [Self] = &[Self::Markdown, Self::Csv];

    /// Formats rendered and structurally validated by `file-engine`.
    pub const RICH: &'static [Self] = &[Self::Xlsx, Self::Pdf, Self::Docx, Self::Pptx];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Markdown => "markdown",
            Self::Csv => "csv",
            Self::Xlsx => "xlsx",
            Self::Pdf => "pdf",
            Self::Docx => "docx",
            Self::Pptx => "pptx",
            Self::WaveAudio => "wave_audio",
            Self::FrameArchive => "frame_archive",
        }
    }

    /// The file extension, without the dot.
    pub const fn extension(self) -> &'static str {
        match self {
            Self::Markdown => "md",
            Self::Csv => "csv",
            Self::Xlsx => "xlsx",
            Self::Pdf => "pdf",
            Self::Docx => "docx",
            Self::Pptx => "pptx",
            Self::WaveAudio => "wav",
            Self::FrameArchive => "zip",
        }
    }

    /// Exact MIME type used by Android's trusted document custody surface.
    pub const fn mime_type(self) -> &'static str {
        match self {
            Self::Markdown => "text/markdown",
            Self::Csv => "text/csv",
            Self::Xlsx => "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
            Self::Pdf => "application/pdf",
            Self::Docx => "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            Self::Pptx => {
                "application/vnd.openxmlformats-officedocument.presentationml.presentation"
            }
            Self::WaveAudio => "audio/wav",
            Self::FrameArchive => "application/zip",
        }
    }

    /// Whether generation belongs to the validated rich-file engine.
    pub const fn is_rich(self) -> bool {
        matches!(self, Self::Xlsx | Self::Pdf | Self::Docx | Self::Pptx)
    }

    /// The internal tool name that produces this format.
    pub const fn tool_name(self) -> &'static str {
        match self {
            Self::Markdown => "artifact.markdown.create",
            Self::Csv => "artifact.csv.create",
            Self::Xlsx => "artifact.xlsx.create",
            Self::Pdf => "artifact.pdf.create",
            Self::Docx => "artifact.docx.create",
            Self::Pptx => "artifact.pptx.create",
            Self::WaveAudio => "media.audio.extract",
            Self::FrameArchive => "media.frames.sample",
        }
    }
}
/// A filename safe to suggest to the system document picker.
///
/// Lowercase ASCII letters, digits, and single hyphens only, bounded in length,
/// with the format's own extension. No path separator, no control character, no
/// leading dot, and nothing a page or a model chose.
pub fn suggested_filename(kind: ArtifactKind, title: &str) -> String {
    const MAX_STEM: usize = 48;
    let mut stem = String::with_capacity(MAX_STEM);
    let mut pending_hyphen = false;
    for character in title.chars() {
        if stem.len() >= MAX_STEM {
            break;
        }
        if character.is_ascii_alphanumeric() {
            if pending_hyphen && !stem.is_empty() {
                stem.push('-');
            }
            pending_hyphen = false;
            stem.push(character.to_ascii_lowercase());
        } else {
            pending_hyphen = true;
        }
    }
    if stem.is_empty() {
        stem.push_str("workspace");
    }
    format!("{stem}.{}", kind.extension())
}

#[cfg(test)]
mod tests {
    use super::{suggested_filename, ArtifactKind};

    #[test]
    fn a_suggested_filename_carries_no_path_and_no_control_character() {
        assert_eq!(
            suggested_filename(ArtifactKind::Markdown, "Compare two laptops"),
            "compare-two-laptops.md"
        );
        assert_eq!(
            suggested_filename(ArtifactKind::Csv, "../../etc/passwd"),
            "etc-passwd.csv"
        );
        assert_eq!(
            suggested_filename(ArtifactKind::Csv, "\u{0}\u{7}"),
            "workspace.csv"
        );
        let long = suggested_filename(ArtifactKind::Markdown, &"a".repeat(200));
        assert_eq!(long.len(), 48 + 3);
    }

    #[test]
    fn every_kind_owns_one_tool_extension_and_mime_type() {
        for kind in ArtifactKind::ALL {
            assert!(
                kind.tool_name().starts_with("artifact.") || kind.tool_name().starts_with("media.")
            );
            assert!(!kind.extension().is_empty());
            assert!(kind.mime_type().contains('/'));
            assert_eq!(kind.is_rich(), ArtifactKind::RICH.contains(kind));
        }
    }
}
