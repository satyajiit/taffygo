// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every closed set in the record schema.
//!
//! One rule, applied nine times: **an unrecognized stored value is refused,
//! never coerced.** A sensitivity that decoded to "probably fine", or a
//! deletion state that decoded to "not started", would be a privacy failure
//! written as a convenience — so the macro below generates a decoder that
//! fails and no default arm exists to fall through to.
//!
//! Its own module because these enumerations are the vocabulary every record
//! type and every lookup is written in, and because they are the part of this
//! area that changes only when the schema does.

use crate::backend::Row;
use crate::error::StorageError;
macro_rules! closed_enum {
    ($name:ident, $what:literal, $( $variant:ident => $text:literal ),+ $(,)?) => {
        #[doc = concat!("A ", $what, ".")]
        ///
        /// Closed: a stored value outside this list does not decode.
        #[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub enum $name {
            $(
                #[doc = concat!("`", $text, "`")]
                $variant
            ),+
        }

        impl $name {
            /// The stored spelling.
            pub fn as_str(self) -> &'static str {
                match self {
                    $( Self::$variant => $text ),+
                }
            }

            /// Decodes a stored spelling.
            pub fn parse(raw: &str) -> Option<Self> {
                match raw {
                    $( $text => Some(Self::$variant), )+
                    _ => None,
                }
            }

            /// Decodes this enumeration from a row column.
            pub fn read(row: &Row, column: usize) -> Result<Self, StorageError> {
                let raw = row.text(column)?;
                Self::parse(raw).ok_or_else(|| StorageError::Malformed {
                    what: $what,
                    detail: raw.to_owned(),
                })
            }
        }
    };
}

closed_enum!(
    WorkspaceStatus,
    "workspace status",
    Active => "ACTIVE",
    Archived => "ARCHIVED",
    Deleting => "DELETING",
    DeletionBlocked => "DELETION_BLOCKED",
    Deleted => "DELETED",
);

closed_enum!(
    DeletionState,
    "deletion state",
    Active => "ACTIVE",
    Deleting => "DELETING",
    Deleted => "DELETED",
    Blocked => "BLOCKED",
);

closed_enum!(
    SourceKind,
    "source kind",
    WebPage => "WEB_PAGE",
    Pdf => "PDF",
    UserFile => "USER_FILE",
    UserText => "USER_TEXT",
    SearchResult => "SEARCH_RESULT",
);

closed_enum!(
    Ownership,
    "source ownership",
    External => "EXTERNAL",
    UserProvided => "USER_PROVIDED",
    Generated => "GENERATED",
);

closed_enum!(
    MembershipState,
    "membership state",
    Included => "INCLUDED",
    Excluded => "EXCLUDED",
    Removed => "REMOVED",
);

closed_enum!(
    Sensitivity,
    "sensitivity",
    Public => "PUBLIC",
    Personal => "PERSONAL",
    Sensitive => "SENSITIVE",
    HighlySensitive => "HIGHLY_SENSITIVE",
    Prohibited => "PROHIBITED",
);

closed_enum!(
    FactClassification,
    "fact classification",
    UserEntered => "USER_ENTERED",
    Extracted => "EXTRACTED",
    Summarized => "SUMMARIZED",
    Inferred => "INFERRED",
);

closed_enum!(
    FactStatus,
    "fact status",
    Candidate => "CANDIDATE",
    Accepted => "ACCEPTED",
    Corrected => "CORRECTED",
    Superseded => "SUPERSEDED",
    Removed => "REMOVED",
);

closed_enum!(
    ProvenanceKind,
    "provenance source kind",
    Dom => "DOM",
    Accessibility => "AX",
    StructuredData => "STRUCTURED_DATA",
    User => "USER",
    Model => "MODEL",
    Tool => "TOOL",
);
