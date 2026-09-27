// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Operand rules shared by model-call admission and canonical intent restore.
//!
//! Structural schema validation answers whether a value has the declared
//! type. This module answers the two operation-specific questions that type
//! tags cannot: whether an address is an HTTPS URL and whether a DOM query's
//! requested result count is inside the snapshot bound. The live parser and
//! durable decoder call these same functions, so restore cannot widen a call.

use super::{ArgumentValue, SuppliedArgument, MAX_ARGUMENT_VALUE_BYTES};

/// Most semantic nodes one DOM query may request.
pub const MAX_DOM_QUERY_RESULTS: u64 = 128;
const MAX_LIBRARY_SEARCH_RESULTS: u64 = 32;
const MAX_MEMORY_SEARCH_RESULTS: u64 = 32;
const MAX_STORE_RESULTS: u64 = crate::action::MAX_STORE_RESULTS as u64;

/// Whether every operation-specific operand in one structurally valid call is valid.
pub(crate) fn call_operands_are_valid(tool_name: &str, arguments: &[SuppliedArgument]) -> bool {
    let addresses_are_valid = arguments.iter().all(|argument| match &argument.value {
        ArgumentValue::Address(value) => is_https_address(value),
        _ => true,
    });
    if !addresses_are_valid {
        return false;
    }
    if tool_name == "browser.form.fill" || tool_name == "browser.form.select" {
        return arguments.iter().all(|argument| match &argument.value {
            ArgumentValue::SuppliedValue(index) => is_valid_supplied_value_index(*index),
            _ => true,
        });
    }
    if tool_name == "library.search" {
        let limit = count_argument(arguments, "limit");
        return limit.is_none_or(|value| value > 0 && value <= MAX_LIBRARY_SEARCH_RESULTS);
    }
    if tool_name == "library.save" {
        return text_argument(arguments, "workspace").is_some_and(is_record_id)
            && text_argument(arguments, "fact").is_some_and(is_record_id)
            && count_argument(arguments, "workspace_revision").is_some_and(|value| value > 0)
            && count_argument(arguments, "entry_revision").is_some();
    }
    if tool_name == "library.remove" {
        return text_argument(arguments, "entry").is_some_and(is_record_id)
            && count_argument(arguments, "entry_revision").is_some_and(|value| value > 0);
    }
    if tool_name == "memory.search" {
        let limit = count_argument(arguments, "limit");
        return limit.is_none_or(|value| value > 0 && value <= MAX_MEMORY_SEARCH_RESULTS);
    }
    if matches!(
        tool_name,
        "history.search" | "history.recent" | "bookmarks.search" | "bookmarks.list"
    ) {
        let limit = count_argument(arguments, "limit");
        return limit.is_none_or(|value| value > 0 && value <= MAX_STORE_RESULTS);
    }
    if tool_name == "memory.save" || tool_name == "memory.update" {
        let scope = choice_argument(arguments, "scope");
        let workspace = text_argument(arguments, "workspace");
        let scope_is_valid = match scope {
            Some("all_tasks") => workspace.is_none(),
            Some("workspace") => workspace.is_some_and(is_record_id),
            _ => false,
        };
        let expiry_is_valid = count_argument(arguments, "expires_at").is_none_or(|value| value > 0);
        let target_is_valid = tool_name == "memory.save"
            || (text_argument(arguments, "memory").is_some_and(is_record_id)
                && count_argument(arguments, "record_revision").is_some_and(|value| value > 0));
        return scope_is_valid && expiry_is_valid && target_is_valid;
    }
    if tool_name == "memory.delete" {
        return text_argument(arguments, "memory").is_some_and(is_record_id)
            && count_argument(arguments, "record_revision").is_some_and(|value| value > 0);
    }
    if tool_name == "python.execute" {
        let entrypoint = choice_argument(arguments, "entrypoint");
        let title = text_argument(arguments, "title");
        let content = text_argument(arguments, "content");
        let values_are_data = title
            .zip(content)
            .is_some_and(|(title, content)| !title.contains('\0') && !content.contains('\0'));
        return values_are_data
            && match entrypoint {
                Some("document") => true,
                Some("spreadsheet") => title.is_some_and(|value| {
                    value.len() <= 31 && !value.chars().any(|ch| "[]:*?/\\".contains(ch))
                }),
                _ => false,
            };
    }
    if tool_name == "media.frames.sample" {
        return count_argument(arguments, "max_frames")
            .is_none_or(|value| (1..=12).contains(&value));
    }
    if tool_name != "browser.dom.query" {
        return true;
    }
    let limit = arguments.iter().find_map(|argument| {
        (argument.name == "limit")
            .then_some(&argument.value)
            .and_then(|value| match value {
                ArgumentValue::Count(value) => Some(*value),
                _ => None,
            })
    });
    is_valid_dom_query_limit(limit)
}

fn count_argument(arguments: &[SuppliedArgument], name: &str) -> Option<u64> {
    arguments.iter().find_map(|argument| match &argument.value {
        ArgumentValue::Count(value) if argument.name == name => Some(*value),
        _ => None,
    })
}

fn text_argument<'a>(arguments: &'a [SuppliedArgument], name: &str) -> Option<&'a str> {
    arguments.iter().find_map(|argument| match &argument.value {
        ArgumentValue::Text(value) if argument.name == name => Some(value.as_str()),
        _ => None,
    })
}

fn choice_argument<'a>(arguments: &'a [SuppliedArgument], name: &str) -> Option<&'a str> {
    arguments.iter().find_map(|argument| match &argument.value {
        ArgumentValue::Choice(value) if argument.name == name => Some(value.as_str()),
        _ => None,
    })
}

fn is_record_id(value: &str) -> bool {
    value.len() == 32
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

/// Whether an optional query bound is meaningful and inside the graph bound.
pub(crate) const fn is_valid_dom_query_limit(limit: Option<u64>) -> bool {
    match limit {
        None => true,
        Some(limit) => limit > 0 && limit <= MAX_DOM_QUERY_RESULTS,
    }
}

/// Whether a value position can name one member of the bounded browser sheet.
pub(crate) const fn is_valid_supplied_value_index(index: u32) -> bool {
    index < crate::field_values::MAX_REQUESTED_FIELDS
}

/// Whether `value` is one bounded absolute HTTPS address.
///
/// This deliberately does not normalize: an action digest binds the exact
/// address the model proposed. It only recognizes a conservative URL shape;
/// the browser still parses it independently and checks origin authority.
pub(crate) fn is_https_address(value: &str) -> bool {
    if value.is_empty()
        || value.len() > MAX_ARGUMENT_VALUE_BYTES
        || value
            .chars()
            .any(|character| character.is_control() || character.is_whitespace())
        || value.contains('\\')
        || contains_directional_control(value)
    {
        return false;
    }
    let Some((scheme, remainder)) = value.split_once("://") else {
        return false;
    };
    if !scheme.eq_ignore_ascii_case("https") {
        return false;
    }
    let authority = remainder.split(['/', '?', '#']).next().unwrap_or_default();
    valid_authority(authority)
}

fn valid_authority(authority: &str) -> bool {
    if authority.is_empty() || authority.contains('@') {
        return false;
    }
    if let Some(bracketed) = authority.strip_prefix('[') {
        let Some((host, suffix)) = bracketed.split_once(']') else {
            return false;
        };
        return host.parse::<std::net::Ipv6Addr>().is_ok() && valid_port_suffix(suffix);
    }
    let colon_count = authority.bytes().filter(|byte| *byte == b':').count();
    let (host, port) = match colon_count {
        0 => (authority, None),
        1 => {
            let Some((host, port)) = authority.rsplit_once(':') else {
                return false;
            };
            (host, Some(port))
        }
        _ => return false,
    };
    valid_host(host) && port.is_none_or(valid_port)
}

fn valid_port_suffix(suffix: &str) -> bool {
    suffix.is_empty() || suffix.strip_prefix(':').is_some_and(valid_port)
}

fn valid_port(port: &str) -> bool {
    !port.is_empty()
        && port.bytes().all(|byte| byte.is_ascii_digit())
        && port.parse::<u16>().is_ok()
}

fn valid_host(host: &str) -> bool {
    if host.is_empty() || host.len() > 253 || !host.is_ascii() {
        return false;
    }
    if host.parse::<std::net::Ipv4Addr>().is_ok() {
        return true;
    }
    if host
        .bytes()
        .all(|byte| byte.is_ascii_digit() || byte == b'.')
    {
        return false;
    }
    host.split('.').all(|label| {
        !label.is_empty()
            && label.len() <= 63
            && label
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
            && label
                .bytes()
                .next()
                .is_some_and(|byte| byte.is_ascii_alphanumeric())
            && label
                .bytes()
                .next_back()
                .is_some_and(|byte| byte.is_ascii_alphanumeric())
    })
}

fn contains_directional_control(value: &str) -> bool {
    value.chars().any(|character| {
        matches!(
            character,
            '\u{061c}'
                | '\u{200e}'
                | '\u{200f}'
                | '\u{202a}'..='\u{202e}'
                | '\u{2066}'..='\u{2069}'
        )
    })
}

#[cfg(test)]
mod tests {
    use super::{
        is_https_address, is_valid_dom_query_limit, is_valid_supplied_value_index,
        MAX_DOM_QUERY_RESULTS,
    };

    #[test]
    fn only_bounded_absolute_https_addresses_are_recognized() {
        for accepted in [
            "https://example.test",
            "HTTPS://Example.test:8443/a?q=1#part",
            "https://127.0.0.1/a",
            "https://[::1]:443/a",
        ] {
            assert!(is_https_address(accepted), "{accepted}");
        }
        for refused in [
            "x",
            "http://example.test",
            "https://",
            "https://user@example.test",
            "https://999.1.1.1",
            "https://example.test:99999",
            "https://bad host.test",
        ] {
            assert!(!is_https_address(refused), "{refused}");
        }
    }

    #[test]
    fn a_query_limit_is_optional_and_otherwise_one_through_the_graph_bound() {
        assert!(is_valid_dom_query_limit(None));
        assert!(is_valid_dom_query_limit(Some(1)));
        assert!(is_valid_dom_query_limit(Some(MAX_DOM_QUERY_RESULTS)));
        assert!(!is_valid_dom_query_limit(Some(0)));
        assert!(!is_valid_dom_query_limit(Some(MAX_DOM_QUERY_RESULTS + 1)));
    }

    #[test]
    fn a_supplied_value_position_is_inside_the_browser_sheet_bound() {
        assert!(is_valid_supplied_value_index(0));
        assert!(is_valid_supplied_value_index(
            crate::field_values::MAX_REQUESTED_FIELDS - 1
        ));
        assert!(!is_valid_supplied_value_index(
            crate::field_values::MAX_REQUESTED_FIELDS
        ));
    }
}
