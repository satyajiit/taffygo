// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict inverse of the frozen v1 canonical encoding.

mod domain;
mod fields;
mod values;

use bip_types::identity::{FrameId, GraphRevision, PageEpoch, SemanticNodeId, TabId};

use super::super::{
    ActionIntent, ActionIntentCodecError, BrowserIntent, DisclosureState, LinkHandleOriginKind,
    ObservedNodeHandle, OpaqueOperandKind, TaskTabTarget, MAX_CANONICAL_ACTION_INTENT_BYTES,
};
use crate::field_values::FieldValueRequestId;

use self::domain::{
    decode_library, decode_media_tool, decode_memory, decode_store, decode_tool_job,
};
use self::values::{
    browser_session_id, node_id, opaque, optional_node, optional_opaque, role_value, scroll_value,
    tab_id,
};

pub(crate) fn decode(input: &[u8]) -> Result<ActionIntent, ActionIntentCodecError> {
    if input.len() > MAX_CANONICAL_ACTION_INTENT_BYTES {
        return Err(ActionIntentCodecError::TooLarge);
    }
    let body = input
        .strip_prefix(super::PREFIX)
        .ok_or(ActionIntentCodecError::InvalidPrefix)?;
    let parsed = fields::parse(body)?;
    let operation = parsed
        .first()
        .filter(|field| field.tag == 0)
        .ok_or(ActionIntentCodecError::MissingField)
        .and_then(|field| fields::byte(field.value))?;
    match operation {
        u8::MAX => decode_tool_job(&parsed),
        254 => decode_library(&parsed).map(ActionIntent::Library),
        253 => decode_memory(&parsed).map(ActionIntent::Memory),
        251 => decode_store(&parsed).map(ActionIntent::Store),
        252 => decode_media_tool(&parsed),
        value => decode_browser(value, &parsed).map(ActionIntent::Browser),
    }
}

fn decode_browser(
    operation: u8,
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    match operation {
        0..=7 => decode_navigation(operation, parsed),
        8 => decode_dom_query(parsed),
        9 => {
            let [_, tab, target] = fields::exact(parsed, [0, 1, 2])?;
            Ok(BrowserIntent::DomRead {
                tab: tab_id(tab)?,
                target: optional_node(target)?,
            })
        }
        10 | 25 => decode_exact_node_action(operation, parsed),
        11 => {
            let [_, tab, direction, target] = fields::exact(parsed, [0, 1, 2, 3])?;
            Ok(BrowserIntent::DomScroll {
                tab: tab_id(tab)?,
                direction: scroll_value(fields::byte(direction)?)?,
                target: optional_node(target)?,
            })
        }
        12 => node_operation(parsed, |tab, form| BrowserIntent::FormInspect { tab, form }),
        13 => decode_form_fill(parsed),
        14 => node_operation(parsed, |tab, control| BrowserIntent::FormSubmit {
            tab,
            control,
        }),
        15 => {
            let [_, tab, address, browser_session] = fields::exact(parsed, [0, 1, 2, 3])?;
            Ok(BrowserIntent::DownloadStart {
                tab: tab_id(tab)?,
                address: fields::address(address)?,
                browser_session_id: browser_session_id(browser_session)?,
            })
        }
        16 => {
            let [_, tab, browser_session] = fields::exact(parsed, [0, 1, 2])?;
            Ok(BrowserIntent::DownloadList {
                tab: tab_id(tab)?,
                browser_session_id: browser_session_id(browser_session)?,
            })
        }
        17 | 21 | 26 => page_operation(operation, parsed),
        18 => node_operation(parsed, |tab, target| BrowserIntent::ImageDescribe {
            tab,
            target,
        }),
        19 => node_operation(parsed, |tab, target| BrowserIntent::ImageReadText {
            tab,
            target,
        }),
        20 => node_operation(parsed, |tab, target| BrowserIntent::VideoInspect {
            tab,
            target,
        }),
        22 | 30 => decode_link_action(operation, parsed),
        23 => decode_supplied_value(parsed, |tab, field, value_request, value_from| {
            BrowserIntent::FormSelect {
                tab,
                field,
                value_request,
                value_from,
            }
        }),
        24 => decode_form_toggle(parsed),
        27 => {
            let [_, tab, browser_session, download_id] = fields::exact(parsed, [0, 1, 2, 3])?;
            Ok(BrowserIntent::DownloadCancel {
                tab: tab_id(tab)?,
                browser_session_id: browser_session_id(browser_session)?,
                download_id: fields::id(download_id)?,
            })
        }
        28 | 29 => {
            let [_, tab] = fields::exact(parsed, [0, 1])?;
            if operation == 28 {
                Ok(BrowserIntent::Reload { tab: tab_id(tab)? })
            } else {
                Ok(BrowserIntent::StopLoading { tab: tab_id(tab)? })
            }
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn decode_form_fill(parsed: &[fields::Field<'_>]) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab, field, value_request, value_from] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
    let value_from = fields::u32_value(value_from)?;
    if !crate::tool::semantic::is_valid_supplied_value_index(value_from) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(BrowserIntent::FormFill {
        tab: tab_id(tab)?,
        field: node_id(field)?,
        value_request: FieldValueRequestId::new(fields::id(value_request)?)
            .map_err(|_| ActionIntentCodecError::InvalidValue)?,
        value_from,
    })
}

fn decode_form_toggle(
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab, field, checked] = fields::exact(parsed, [0, 1, 2, 3])?;
    let checked = match fields::byte(checked)? {
        0 => false,
        1 => true,
        _ => return Err(ActionIntentCodecError::InvalidValue),
    };
    Ok(BrowserIntent::FormToggle {
        tab: tab_id(tab)?,
        field: node_id(field)?,
        checked,
    })
}

fn decode_exact_node_action(
    operation: u8,
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    match operation {
        10 => {
            let [_, tab, frame, epoch, revision, node, origin_kind, origin_value, expected_state] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6, 7, 8])?;
            let disclosure = fields::byte(expected_state)?;
            let expected_state = if disclosure == DisclosureState::NONE_WIRE_TAG {
                None
            } else {
                Some(
                    DisclosureState::from_wire_tag(disclosure)
                        .ok_or(ActionIntentCodecError::InvalidValue)?,
                )
            };
            Ok(BrowserIntent::DomClick {
                target: decode_observed_node_handle(
                    tab,
                    frame,
                    epoch,
                    revision,
                    node,
                    origin_kind,
                    origin_value,
                )?,
                expected_state,
            })
        }
        25 => {
            let [_, tab, frame, epoch, revision, node, origin_kind, origin_value] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6, 7])?;
            Ok(BrowserIntent::DomFocus {
                target: decode_observed_node_handle(
                    tab,
                    frame,
                    epoch,
                    revision,
                    node,
                    origin_kind,
                    origin_value,
                )?,
            })
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn decode_supplied_value(
    parsed: &[fields::Field<'_>],
    build: impl FnOnce(TabId, SemanticNodeId, FieldValueRequestId, u32) -> BrowserIntent,
) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab, field, value_request, value_from] = fields::exact(parsed, [0, 1, 2, 3, 4])?;
    let value_from = fields::u32_value(value_from)?;
    if !crate::tool::semantic::is_valid_supplied_value_index(value_from) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(build(
        tab_id(tab)?,
        node_id(field)?,
        FieldValueRequestId::new(fields::id(value_request)?)
            .map_err(|_| ActionIntentCodecError::InvalidValue)?,
        value_from,
    ))
}

fn decode_link_action(
    operation: u8,
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    let (target_fields, session) = if operation == 30 {
        let (session, target_fields) = parsed
            .split_last()
            .ok_or(ActionIntentCodecError::MissingField)?;
        if session.tag != 8 {
            return Err(ActionIntentCodecError::MissingField);
        }
        (target_fields, Some(browser_session_id(session.value)?))
    } else {
        (parsed, None)
    };
    let [_, tab, frame, epoch, revision, node, origin_kind, origin_value] =
        fields::exact(target_fields, [0, 1, 2, 3, 4, 5, 6, 7])?;
    let target =
        decode_observed_node_handle(tab, frame, epoch, revision, node, origin_kind, origin_value)?;
    Ok(match session {
        Some(browser_session_id) => BrowserIntent::DownloadFromLink {
            target,
            browser_session_id,
        },
        None => BrowserIntent::LinkOpen { target },
    })
}

fn decode_observed_node_handle(
    tab: &[u8],
    frame: &[u8],
    epoch: &[u8],
    revision: &[u8],
    node: &[u8],
    origin_kind: &[u8],
    origin_value: &[u8],
) -> Result<ObservedNodeHandle, ActionIntentCodecError> {
    let origin_kind = match fields::byte(origin_kind)? {
        0 => LinkHandleOriginKind::Tuple,
        1 => LinkHandleOriginKind::Opaque,
        _ => return Err(ActionIntentCodecError::InvalidValue),
    };
    Ok(ObservedNodeHandle::from_parts(
        tab_id(tab)?,
        FrameId::new(fields::id(frame)?),
        PageEpoch::new(fields::id(epoch)?),
        GraphRevision(fields::u64_value(revision)?),
        node_id(node)?,
        origin_kind,
        fields::origin_value(origin_value)?,
    ))
}

fn decode_navigation(
    operation: u8,
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    match operation {
        0 => {
            let [_, tab, address, new_tab] = fields::exact(parsed, [0, 1, 2, 3])?;
            Ok(BrowserIntent::Navigate {
                tab: tab_id(tab)?,
                address: fields::address(address)?,
                new_tab: fields::boolean(new_tab)?,
            })
        }
        1 => {
            let [_, tab, query] = fields::exact(parsed, [0, 1, 2])?;
            Ok(BrowserIntent::Search {
                tab: tab_id(tab)?,
                query: opaque(query, OpaqueOperandKind::SearchQuery)?,
            })
        }
        2 | 3 => {
            let [_, tab] = fields::exact(parsed, [0, 1])?;
            if operation == 2 {
                Ok(BrowserIntent::HistoryBack { tab: tab_id(tab)? })
            } else {
                Ok(BrowserIntent::HistoryForward { tab: tab_id(tab)? })
            }
        }
        4 => {
            let [_, context, address] = fields::exact(parsed, [0, 1, 2])?;
            Ok(BrowserIntent::TabsOpen {
                context: tab_id(context)?,
                address: fields::optional_address(address)?,
            })
        }
        5 => {
            let [_, context, browser_session] = fields::exact(parsed, [0, 1, 2])?;
            Ok(BrowserIntent::TabsList {
                context: tab_id(context)?,
                browser_session_id: browser_session_id(browser_session)?,
            })
        }
        6 | 7 => {
            let [_, context, browser_session, target, frame, epoch, revision] =
                fields::exact(parsed, [0, 1, 2, 3, 4, 5, 6])?;
            let context = tab_id(context)?;
            let target = TaskTabTarget::new(
                browser_session_id(browser_session)?,
                tab_id(target)?,
                FrameId::new(fields::id(frame)?),
                PageEpoch::new(fields::id(epoch)?),
                GraphRevision(fields::u64_value(revision)?),
            );
            if operation == 6 {
                Ok(BrowserIntent::TabsActivate { context, target })
            } else {
                Ok(BrowserIntent::TabsClose { context, target })
            }
        }
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}

fn decode_dom_query(parsed: &[fields::Field<'_>]) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab, within, role, text, limit] = fields::exact(parsed, [0, 1, 2, 3, 4, 5])?;
    let limit = fields::optional_u64(limit)?;
    if !crate::tool::semantic::is_valid_dom_query_limit(limit) {
        return Err(ActionIntentCodecError::InvalidValue);
    }
    Ok(BrowserIntent::DomQuery {
        tab: tab_id(tab)?,
        within: optional_node(within)?,
        role: fields::optional_byte(role)?.map(role_value).transpose()?,
        text: optional_opaque(text, OpaqueOperandKind::DomQueryText)?,
        limit,
    })
}

fn node_operation(
    parsed: &[fields::Field<'_>],
    build: impl FnOnce(TabId, SemanticNodeId) -> BrowserIntent,
) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab, target] = fields::exact(parsed, [0, 1, 2])?;
    Ok(build(tab_id(tab)?, node_id(target)?))
}

fn page_operation(
    operation: u8,
    parsed: &[fields::Field<'_>],
) -> Result<BrowserIntent, ActionIntentCodecError> {
    let [_, tab] = fields::exact(parsed, [0, 1])?;
    let tab = tab_id(tab)?;
    match operation {
        17 => Ok(BrowserIntent::SelectionRead { tab }),
        21 => Ok(BrowserIntent::PdfInspect { tab }),
        26 => Ok(BrowserIntent::PageScreenshotInspect { tab }),
        _ => Err(ActionIntentCodecError::InvalidValue),
    }
}
