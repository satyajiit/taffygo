// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic Markdown and comma-separated exports (domain model section 16,
//! privacy specification section 15).
//!
//! # Byte-identical for identical input
//!
//! Nothing here reads a clock, a locale, a random source, or an environment
//! variable, and nothing depends on the order the caller happened to collect
//! its rows: sources, facts, and evidence are all sorted into a total order
//! before a byte is written. Two runs over the same records produce the same
//! bytes, and so do two runs over the same records shuffled.
//!
//! # Every value carries its sources
//!
//! A row is a portable [`crate::records::Fact`] view and its citations are
//! [`crate::records::ProvenanceLocator`] views. No model text
//! reaches an artifact through this module, and a fact that was derived
//! externally without a locator is refused rather than exported uncited.
//!
//! # Untrusted values are neutralized, not trusted
//!
//! Every value is treated as hostile input, because a value that came off a
//! page is. Control characters are removed, Markdown table syntax is escaped,
//! addresses are written as inline code rather than as links, and a
//! comma-separated cell that a spreadsheet would evaluate as a formula is
//! prefixed so it stays text.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`kind`] | The format, its extension, and the filename it suggests |
//! | [`request`] | What generation is given |
//! | [`error`] | Why generation refuses |
//! | [`document`] | What generation returns, and its checksum |
//! | [`cell`] | Neutralizing one untrusted value for one destination |
//! | [`layout`] | Putting sources and facts into a total order |
//! | [`markdown`], [`csv`] | The two formats, and nothing else |

/// Appends a formatted line to an artifact.
///
/// Writing to a `String` cannot fail. The result is discarded rather than
/// unwrapped, so no path through artifact generation can panic.
///
/// Declared here, before the renderer modules, because textual macro scope is
/// what makes it visible to them without exporting it from the crate.
macro_rules! append_line {
    ($out:expr, $($arg:tt)*) => {{
        let _ = ::core::fmt::Write::write_fmt(&mut $out, format_args!($($arg)*));
    }};
}

mod cell;
mod csv;
mod document;
mod error;
mod kind;
mod layout;
mod markdown;
mod record;
mod request;
mod writer;

pub use self::document::{Artifact, ContentChecksum};
pub use self::error::ArtifactError;
pub use self::kind::{suggested_filename, ArtifactKind};
pub use self::record::{ArtifactCustody, ArtifactRecord};
pub use self::request::{ArtifactRequest, CitedFact};

use self::layout::prepare;

fn generate_bounded(
    request: &ArtifactRequest,
    kind: ArtifactKind,
    limit: u64,
) -> Result<Artifact, ArtifactError> {
    if kind.is_rich() {
        return Err(ArtifactError::RichFormatRequiresFileEngine);
    }
    let (sources, rows) = prepare(request)?;
    let rendered = match kind {
        ArtifactKind::Markdown => self::markdown::render(request, &sources, &rows, limit),
        ArtifactKind::Csv => self::csv::render(&sources, &rows, limit),
        ArtifactKind::Xlsx | ArtifactKind::Pdf | ArtifactKind::Docx | ArtifactKind::Pptx => {
            return Err(ArtifactError::RichFormatRequiresFileEngine);
        }
        ArtifactKind::WaveAudio | ArtifactKind::FrameArchive => {
            return Err(ArtifactError::BrowserCustodiedFormat);
        }
    };
    let content = rendered.finish()?;
    Ok(Artifact {
        artifact_id: request.artifact_id.clone(),
        kind,
        checksum: ContentChecksum::of(content.as_bytes()),
        fact_count: u64::try_from(rows.len()).unwrap_or(u64::MAX),
        source_count: u64::try_from(sources.len()).unwrap_or(u64::MAX),
        content,
    })
}

/// Generates an artifact.
///
/// Deterministic by construction: the material is ordered by [`layout`], every
/// value is neutralized by [`cell`], and neither renderer consults a locale, a
/// clock, or the order the caller collected its rows.
pub fn generate(request: &ArtifactRequest, kind: ArtifactKind) -> Result<Artifact, ArtifactError> {
    generate_bounded(request, kind, u64::MAX)
}

/// Generates an artifact and refuses one larger than `limit` bytes.
pub fn generate_within(
    request: &ArtifactRequest,
    kind: ArtifactKind,
    limit: u64,
) -> Result<Artifact, ArtifactError> {
    generate_bounded(request, kind, limit)
}
