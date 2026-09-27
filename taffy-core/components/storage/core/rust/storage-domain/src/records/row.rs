// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading one column of one row, once.
//!
//! Three total helpers shared by every record decoder. They exist so that a
//! missing column, a wrong type, and an unparsable identifier each produce the
//! same shaped [`StorageError`] wherever they happen, rather than each
//! decoder inventing its own.

use crate::backend::Row;
use crate::clock::Timestamp;
use crate::error::StorageError;
pub(super) fn timestamp(row: &Row, column: usize) -> Result<Timestamp, StorageError> {
    let raw = row.text(column)?;
    Timestamp::new(raw).map_err(|_| StorageError::Malformed {
        what: "a stored timestamp",
        detail: raw.to_owned(),
    })
}

pub(super) fn maybe_timestamp(row: &Row, column: usize) -> Result<Option<Timestamp>, StorageError> {
    match row.maybe_text(column)? {
        None => Ok(None),
        Some(raw) => Timestamp::new(raw)
            .map(Some)
            .map_err(|_| StorageError::Malformed {
                what: "a stored timestamp",
                detail: raw.to_owned(),
            }),
    }
}

pub(super) fn id<T, F>(
    row: &Row,
    column: usize,
    parse: F,
    what: &'static str,
) -> Result<T, StorageError>
where
    F: Fn(&str) -> Option<T>,
{
    let raw = row.text(column)?;
    parse(raw).ok_or_else(|| StorageError::Malformed {
        what,
        detail: raw.to_owned(),
    })
}
