// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic rich-file generation behind one narrow seam.
//!
//! [`generate`] accepts one typed first-party specification, enforces a caller
//! supplied [`Limits`] value before and during allocation, produces bytes, and
//! validates the complete result before returning it. [`validate`] is the same
//! structural validator exposed separately for bytes crossing a storage or
//! process seam. Neither function reads a clock, locale, random source, file,
//! network resource, or environment variable.
//!
//! OOXML packages use a deliberately small ZIP-store implementation. It emits
//! fixed timestamps and sorted paths, and accepts no compression, encryption,
//! data descriptor, ZIP64 field, duplicate path, absolute path, or traversal.
//! Spreadsheet cells are typed as text, number, boolean, or blank; there is no
//! formula input type and text beginning with formula sigils remains an inline
//! string. PDF output uses only the built-in Helvetica font.
//!
//! This module proves package structure, relationship locality, CRCs, XML
//! well-formedness, PDF object offsets, and format-specific required parts. It
//! does not claim visual/render fidelity, PDF merge/split, macros, executable
//! content, arbitrary templates, charts, or images.

#![forbid(unsafe_code)]

mod docx;
mod error;
mod limits;
mod model;
mod ooxml;
mod pdf;
mod report;
mod sink;
mod validate;
mod xml;
mod zip;

pub use error::{FileError, InputIssue, LimitKind, PackageIssue};
pub use limits::Limits;
pub use model::{
    Block, Cell, Deck, Document, FileSpec, Format, GeneratedFile, PdfDocument, PdfPage, Sheet,
    Slide, Validation, Workbook,
};
pub use report::{
    generate_sourced_report, FindingBasis, ReportFinding, ReportSource, SourcedReport,
};

/// Generate and structurally validate one deterministic file.
pub fn generate(spec: &FileSpec, limits: Limits) -> Result<GeneratedFile, FileError> {
    limits.validate()?;
    let bytes = match spec {
        FileSpec::Workbook(workbook) => ooxml::xlsx::generate(workbook, limits)?,
        FileSpec::Document(document) => docx::generate(document, limits)?,
        FileSpec::Deck(deck) => ooxml::pptx::generate(deck, limits)?,
        FileSpec::Pdf(document) => pdf::generate(document, limits)?,
    };
    let format = spec.format();
    let validation = validate(format, &bytes, limits)?;
    Ok(GeneratedFile::new(format, bytes, validation))
}

/// Validate bytes against the closed structure emitted for `format`.
pub fn validate(format: Format, bytes: &[u8], limits: Limits) -> Result<Validation, FileError> {
    limits.validate()?;
    validate::validate(format, bytes, limits)
}
