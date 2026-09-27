// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What was dropped decoding a document, and why.
//!
//! A dropped entry is never a silent one. Every defect carries where it was
//! and why, so a bad publish is visible as configuration state rather than as
//! a model that quietly stopped being offered.
//!
//! Its own module because these types are the *output* contract of decoding:
//! the settings surface and the tests read them, and neither needs to know how
//! a field is parsed.

use crate::catalog::types::CatalogDocument;
/// Where a defect was found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DefectLocation {
    /// The document header.
    Document,
    /// A provider entry, by position and by id when the id was readable.
    Provider {
        /// Position in the `providers` array.
        index: usize,
        /// The entry's id, when it decoded.
        provider_id: Option<String>,
    },
    /// A model entry, by position and by id when the id was readable.
    Model {
        /// Position in the `models` array.
        index: usize,
        /// The entry's id, when it decoded.
        model_id: Option<String>,
    },
}

/// Why an entry was dropped.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DefectReason {
    /// The entry declared a schema version this build does not understand.
    UnsupportedSchemaVersion {
        /// The declared version.
        declared: u32,
    },
    /// A required field was absent.
    MissingField {
        /// Field name.
        field: &'static str,
    },
    /// A field had the wrong JSON shape.
    WrongType {
        /// Field name.
        field: &'static str,
    },
    /// A closed enumeration carried a value this build does not know.
    UnknownEnumValue {
        /// Field name.
        field: &'static str,
        /// The value as published.
        value: String,
    },
    /// A field failed its own validation.
    Invalid {
        /// Field name.
        field: &'static str,
        /// Why it failed.
        detail: String,
    },
    /// The entry repeated an id already seen in this document.
    DuplicateEntry,
    /// The document carried more entries than the accepted bound.
    TooManyEntries,
}

/// One dropped entry.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CatalogDefect {
    /// Where it was.
    pub location: DefectLocation,
    /// Why it was dropped.
    pub reason: DefectReason,
}

/// A decoded document plus everything that was dropped decoding it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParsedCatalog {
    /// The entries that survived.
    pub document: CatalogDocument,
    /// The entries that did not, and why.
    pub defects: Vec<CatalogDefect>,
}

impl ParsedCatalog {
    /// Whether every entry in the document decoded.
    pub fn is_clean(&self) -> bool {
        self.defects.is_empty()
    }
}
