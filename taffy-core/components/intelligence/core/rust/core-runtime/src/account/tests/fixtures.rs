// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::account::crypto::ReferenceSha256;
use crate::account::{
    AccountAuthMethod, AccountEffect, AccountProtocol, AccountScope, AuthFlowId,
    AuthorizationEntropy, AuthorizationIntent, AuthorizationRequestPlan, RedirectBindingId,
    SecretHandle,
};
use crate::contract::Deadline;

pub(super) fn intent(value: &str) -> AuthorizationIntent {
    AuthorizationIntent {
        flow_id: AuthFlowId::new(value).unwrap_or_else(|_| unreachable!()),
        auth_method: AccountAuthMethod::Github,
        email: None,
        redirect_binding: RedirectBindingId::new("primary_auth_callback")
            .unwrap_or_else(|_| unreachable!()),
        scopes: vec![AccountScope::OpenId, AccountScope::Email],
        issued_at_millis: 10,
        deadline: Deadline::from_millis(500),
    }
}

pub(super) fn entropy() -> AuthorizationEntropy {
    AuthorizationEntropy::new(std::array::from_fn(
        |position| if position < 32 { 1 } else { 2 },
    ))
}

pub(super) fn open_flow(protocol: &mut AccountProtocol, value: &str) -> AuthorizationRequestPlan {
    let flow_id = AuthFlowId::new(value).unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        protocol.begin_authorization(intent(value), 10),
        Ok(AccountEffect::RequestSecureEntropy { bytes: 64, .. })
    ));
    let Ok(AccountEffect::StorePkceVerifier { verifier, .. }) =
        protocol.accept_authorization_entropy(&flow_id, &entropy(), &ReferenceSha256)
    else {
        unreachable!()
    };
    assert_eq!(
        String::from_utf8(verifier.into_bytes()).unwrap_or_default(),
        "AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI"
    );
    match protocol.accept_pkce_verifier_handle(
        &flow_id,
        SecretHandle::new(format!("verifier-{value}")).unwrap_or_else(|_| unreachable!()),
    ) {
        Ok(AccountEffect::OpenAuthorization(plan)) => plan,
        Ok(_) | Err(_) => unreachable!(),
    }
}
