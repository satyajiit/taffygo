// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The store family (canonical family 251): a kind byte, the context tab,
//! then the words as an opaque operand and the cap for the kinds that take
//! them, each under sequential tags.

use super::super::StoreIntent;
use super::{field, opaque};

pub(super) fn encode(out: &mut Vec<u8>, intent: &StoreIntent) {
    field(out, 0, &[251]);
    match intent {
        StoreIntent::HistorySearch { tab, query, limit } => {
            field(out, 1, &[0]);
            field(out, 2, tab.as_str().as_bytes());
            opaque(out, 3, query);
            field(out, 4, &limit.to_le_bytes());
        }
        StoreIntent::HistoryRecent { tab, limit } => {
            field(out, 1, &[1]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, &limit.to_le_bytes());
        }
        StoreIntent::BookmarksSearch { tab, query, limit } => {
            field(out, 1, &[2]);
            field(out, 2, tab.as_str().as_bytes());
            opaque(out, 3, query);
            field(out, 4, &limit.to_le_bytes());
        }
        StoreIntent::BookmarksList { tab, limit } => {
            field(out, 1, &[3]);
            field(out, 2, tab.as_str().as_bytes());
            field(out, 3, &limit.to_le_bytes());
        }
        StoreIntent::OpenTabsList { tab } => {
            field(out, 1, &[4]);
            field(out, 2, tab.as_str().as_bytes());
        }
    }
}
