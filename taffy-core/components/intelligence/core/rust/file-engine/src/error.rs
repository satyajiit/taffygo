// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core::fmt;

/// One bounded resource owned by the file engine.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LimitKind {
    /// Final or intermediate encoded bytes.
    OutputBytes,
    /// Source text bytes across the typed input.
    TextBytes,
    /// Spreadsheet sheets.
    Sheets,
    /// Spreadsheet rows.
    Rows,
    /// Spreadsheet columns.
    Columns,
    /// Non-blank spreadsheet cells.
    Cells,
    /// Document blocks.
    Blocks,
    /// Presentation slides.
    Slides,
    /// PDF pages.
    Pages,
    /// Lines on one PDF page.
    Lines,
    /// Parts in one package.
    PackageParts,
}

/// A typed input violated a format invariant.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InputIssue {
    /// Text contained a character XML 1.0 and PDF cannot safely carry.
    UnsupportedCharacter,
    /// A sheet name was empty, too long, reserved, duplicated, or contained a forbidden mark.
    InvalidSheetName,
    /// A numeric cell was NaN or infinite.
    NonFiniteNumber,
    /// A heading level was outside 1 through 6.
    InvalidHeadingLevel,
    /// A required title, page, sheet, row, slide, or block collection was empty.
    EmptyRequiredValue,
    /// A caller constructed nonsensical limits.
    InvalidLimits,
    /// A source identity was empty or appeared more than once.
    InvalidSourceIdentity,
    /// A finding cited no source, or named a source the report did not carry.
    InvalidSourceAttribution,
}

/// A byte package failed structural validation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PackageIssue {
    /// The byte stream ended before a declared structure did.
    Truncated,
    /// A signature, version, offset, length, or ordering field was invalid.
    Malformed,
    /// A package used compression, encryption, ZIP64, or another unsupported feature.
    UnsupportedFeature,
    /// A package path was absolute, traversing, duplicated, malformed, or out of order.
    UnsafePath,
    /// Stored bytes did not match their CRC.
    ChecksumMismatch,
    /// XML was not well formed or used a forbidden declaration.
    InvalidXml,
    /// A required part or closed relationship was absent or inconsistent.
    MissingStructure,
    /// Active, external, formula, or otherwise forbidden content was present.
    ForbiddenContent,
    /// A PDF cross-reference or object relationship was inconsistent.
    InvalidPdf,
}

/// Every refusal from generation and validation is closed and content-free.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FileError {
    /// A resource would exceed the caller's declared maximum.
    LimitExceeded {
        /// Which resource was bounded.
        kind: LimitKind,
        /// The declared maximum.
        maximum: usize,
    },
    /// Typed source input was invalid.
    InvalidInput(InputIssue),
    /// Encoded bytes were structurally invalid.
    InvalidPackage(PackageIssue),
}

impl fmt::Display for FileError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::LimitExceeded { kind, maximum } => {
                write!(formatter, "{kind:?} exceeds its maximum of {maximum}")
            }
            Self::InvalidInput(issue) => write!(formatter, "invalid file input: {issue:?}"),
            Self::InvalidPackage(issue) => write!(formatter, "invalid file package: {issue:?}"),
        }
    }
}

impl std::error::Error for FileError {}
