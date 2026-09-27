// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::fixtures::entropy;
use crate::account::{AccountSubjectId, GoogleNonceEntropy, RedirectState, SecretHandle};

#[test]
fn secure_handles_do_not_render_lookup_keys_in_debug_output() {
    let handle = SecretHandle::new("browser-secure-store-key").unwrap_or_else(|_| unreachable!());
    assert!(!format!("{handle:?}").contains("browser-secure-store-key"));
    let state =
        RedirectState::new("sensitive-state-value-00000000000").unwrap_or_else(|_| unreachable!());
    assert!(!format!("{state:?}").contains("sensitive-state-value"));
    assert!(!format!("{:?}", entropy()).contains("1, 1"));
    assert!(!format!("{:?}", GoogleNonceEntropy::new([7; 32])).contains("7, 7"));
    let subject =
        AccountSubjectId::new("provider-user-identifier").unwrap_or_else(|_| unreachable!());
    assert!(!format!("{subject:?}").contains("provider-user-identifier"));
}
