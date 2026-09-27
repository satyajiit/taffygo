// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tool declarations shared by every direct provider request.

use super::{Json, ToolDeclaration};
use crate::request::CacheRetention;
use crate::wire::dialect::{ToolPlacement, ToolWrapping};

pub(super) fn write(
    json: &mut Json<'_>,
    placement: &ToolPlacement,
    tools: &[ToolDeclaration<'_>],
    retention: CacheRetention,
) {
    json.key(placement.list_key);
    json.open_array();
    let declarations = tools.iter().enumerate().map(|(index, tool)| {
        let retention = if index + 1 == tools.len() {
            retention
        } else {
            CacheRetention::None
        };
        (tool, retention)
    });
    match placement.wrapping {
        ToolWrapping::Direct => {
            for (tool, retention) in declarations {
                json.open_object();
                write_declaration(json, placement, tool, retention);
                json.close_object();
            }
        }
        ToolWrapping::Envelope { tag, inner_key } => {
            for (tool, retention) in declarations {
                json.open_object();
                json.key(tag.key);
                json.text(tag.value);
                match inner_key {
                    Some(key) => {
                        json.key(key);
                        json.open_object();
                        write_declaration(json, placement, tool, retention);
                        json.close_object();
                    }
                    None => write_declaration(json, placement, tool, retention),
                }
                json.close_object();
            }
        }
        ToolWrapping::Grouped { group_key } => {
            json.open_object();
            json.key(group_key);
            json.open_array();
            for (tool, retention) in declarations {
                json.open_object();
                write_declaration(json, placement, tool, retention);
                json.close_object();
            }
            json.close_array();
            json.close_object();
        }
    }
    json.close_array();
}

fn write_declaration(
    json: &mut Json<'_>,
    placement: &ToolPlacement,
    tool: &ToolDeclaration<'_>,
    retention: CacheRetention,
) {
    json.key(placement.name_key);
    json.text(tool.name);
    json.key(placement.description_key);
    json.text(tool.description);
    json.key(placement.schema_key);
    json.value(tool.parameters);
    // JSON `null` where the family names the field. See `strict_key`: `null`
    // and `false` are different answers, and an absent key is a third.
    if let Some(strict_key) = placement.strict_key {
        json.key(strict_key);
        json.null();
    }
    write_cache_marker(json, placement, retention);
}

fn write_cache_marker(json: &mut Json<'_>, placement: &ToolPlacement, retention: CacheRetention) {
    let Some(cache) = placement
        .prefix_cache
        .filter(|_| retention != CacheRetention::None)
    else {
        return;
    };
    json.key(cache.key);
    json.open_object();
    json.key(cache.kind.key);
    json.text(cache.kind.value);
    if retention == CacheRetention::Long {
        json.key(cache.extended_retention.key);
        json.text(cache.extended_retention.value);
    }
    json.close_object();
}
