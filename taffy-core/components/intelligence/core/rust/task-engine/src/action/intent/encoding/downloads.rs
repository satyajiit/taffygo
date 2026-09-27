// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Download operations share one manager identity and keep link URLs absent.

use super::field;

pub(super) fn start(
    out: &mut Vec<u8>,
    address: &str,
    browser_session_id: &crate::BrowserSessionId,
) {
    field(out, 2, address.as_bytes());
    field(out, 3, browser_session_id.as_str().as_bytes());
}

pub(super) fn cancel(
    out: &mut Vec<u8>,
    browser_session_id: &crate::BrowserSessionId,
    download_id: &str,
) {
    field(out, 2, browser_session_id.as_str().as_bytes());
    field(out, 3, download_id.as_bytes());
}

pub(super) fn from_link(
    out: &mut Vec<u8>,
    target: &crate::action::ObservedNodeHandle,
    browser_session_id: &crate::BrowserSessionId,
) {
    super::encode_observed_node_target(out, target);
    field(out, 8, browser_session_id.as_str().as_bytes());
}
