// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The header names an adapter fills from the secret store.
//!
//! Header names are protocol detail and are not secret; the values never
//! appear in this crate at all. Its own module so that "what does this crate
//! know about credentials" has a one-file answer: the name of a header, and
//! the list of methods a provider declares.

use crate::catalog::{AuthMethod, Provider, WireApi};
use crate::credential::AuthType;
/// The header names an adapter fills from the secret store.
///
/// Header names are protocol detail and are not secret. The values never appear
/// in this crate.
pub fn secret_header_names(wire_api: WireApi, auth_type: AuthType) -> Vec<String> {
    let name = match (wire_api, auth_type) {
        // The Cloud Code Assist family is named beside the families that
        // answer the same way on either method, rather than left to the
        // subscription arm alone. It is reached only on a subscription today,
        // so the key pairing is unreachable — but it is written out because a
        // pair nobody named would otherwise be a family whose header nobody
        // decided, and this match has no catch-all to hide that behind.
        (_, AuthType::Oauth)
        | (
            WireApi::OpenAiResponses
            | WireApi::OpenAiCompletions
            | WireApi::OpenAiCodexResponses
            | WireApi::GoogleCloudCodeAssist,
            _,
        ) => "authorization",
        (WireApi::AnthropicMessages, AuthType::ApiKey) => "x-api-key",
        (WireApi::GoogleGenerativeLanguage, AuthType::ApiKey) => "x-goog-api-key",
    };
    vec![name.to_owned()]
}

/// The auth methods a provider entry declares, for a settings surface.
pub fn declared_methods(provider: &Provider) -> Vec<AuthMethod> {
    provider.auth_methods.clone()
}
