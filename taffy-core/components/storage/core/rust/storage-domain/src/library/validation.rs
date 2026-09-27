// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{LibraryEntry, LibrarySource, MAX_LIBRARY_OPERATION_ID_BYTES, MAX_LIBRARY_SOURCES};
use crate::workspace::{
    MAX_DISPLAY_NAME_BYTES, MAX_FIELD_BYTES, MAX_HOST_BYTES, MAX_TITLE_BYTES, MAX_VALUE_BYTES,
};

pub(super) fn valid_entry(entry: &LibraryEntry) -> bool {
    entry.revision > 0
        && entry.source_workspace_revision > 0
        && valid_text(&entry.collection_name, MAX_DISPLAY_NAME_BYTES)
        && valid_text(&entry.field, MAX_FIELD_BYTES)
        && valid_text(&entry.original_value, MAX_VALUE_BYTES)
        && entry
            .correction
            .as_deref()
            .is_none_or(|value| valid_text(value, MAX_VALUE_BYTES))
        && !entry.sources.is_empty()
        && entry.sources.len() <= MAX_LIBRARY_SOURCES
        && strictly_ordered_sources(&entry.sources)
        && entry.sources.iter().all(valid_source)
        && entry.last_checked_epoch_ms
            == entry
                .sources
                .iter()
                .map(|source| source.observed_at_epoch_ms)
                .max()
                .unwrap_or(0)
        && entry.captured_at_epoch_ms >= entry.last_checked_epoch_ms
}

pub(super) fn valid_operation_id(value: &str) -> bool {
    valid_text(value, MAX_LIBRARY_OPERATION_ID_BYTES)
}

fn valid_source(source: &LibrarySource) -> bool {
    valid_text(&source.title, MAX_TITLE_BYTES) && valid_host(&source.host)
}

fn valid_text(value: &str, maximum: usize) -> bool {
    !value.is_empty()
        && value.len() <= maximum
        && value.trim() == value
        && !value.chars().any(char::is_control)
}

fn valid_host(host: &str) -> bool {
    if host.is_empty()
        || host.len() > MAX_HOST_BYTES
        || !host.is_ascii()
        || host.contains(['/', '?', '#', '@'])
    {
        return false;
    }
    if host.contains(':') {
        return host.parse::<std::net::Ipv6Addr>().is_ok();
    }
    host.bytes()
        .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-'))
}

fn strictly_ordered_sources(sources: &[LibrarySource]) -> bool {
    sources.windows(2).all(|pair| {
        pair.first()
            .zip(pair.get(1))
            .is_some_and(|(a, b)| a.source_id < b.source_id)
    })
}
