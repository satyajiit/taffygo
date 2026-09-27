// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The address a composed request names, and the checks it must pass before it
//! is handed over.
//!
//! Both kinds of check live here rather than at the call site because both are
//! refusals about the *composition*: the browser refuses the same shapes and
//! would answer `INVALID_RESULT`, which names the transport for a value this
//! side chose.
//!
//! There are two address rules and they are deliberately not written in terms
//! of each other (decision 0096 section 2). A catalog address is *judged* —
//! trimmed to the one origin spelling a served document may name. A person's
//! own address is *recognized* — carried whole to a browser that will compare
//! it with the string that person registered. Relaxing either one here cannot
//! relax the one beside it, which is the property that lets the catalog rule
//! stay as narrow as it is while a server on somebody's desk is still
//! reachable.

use model_router::catalog::CatalogLayer;
use model_router::route::{Recipient, RouteCandidate};

use super::{ComposedModelAttempt, ComposedModelTurn, ModelTurnError};

/// The address one composed request names, and the rule that judges it.
type Destination = (String, core_service_types::ModelEndpointKind);

/// The `https` origin of a catalog endpoint, or nothing.
///
/// Several catalog entries carry a path (`https://api.openai.com/v1`), and the
/// browser refuses anything that is not a bare origin — deliberately, because
/// the path belongs to the compiled-in wire table on that side. Trimming here
/// rather than forwarding is what keeps the two halves agreeing; a port or
/// credentials in the authority are refused rather than trimmed, because both
/// change *which* host is reached.
pub(super) fn origin_of(endpoint: &str) -> Option<String> {
    let rest = endpoint.strip_prefix("https://")?;
    let authority = rest.split('/').next()?;
    if authority.is_empty()
        || authority.contains('@')
        || authority.contains(':')
        || authority.bytes().any(|byte| byte.is_ascii_uppercase())
    {
        return None;
    }
    Some(format!("https://{authority}"))
}

/// The base URL a person registered for their own server, whole.
///
/// Nothing is trimmed and nothing is parsed, which is the whole difference
/// from [`origin_of`]. The scheme, the port and the base path are the address
/// that person typed and the browser wrote down; a `/v1` dropped here is a
/// request to a server's root, and a port dropped here is a request to a
/// different server entirely.
///
/// It judges no further than "there is an address here", and that is the
/// deliberate part rather than an omission. The browser answers this by byte
/// equality against its register (decision 0096 section 1), so a second
/// opinion in the sandbox could only ever disagree with the authority — and
/// the day it did, a server somebody actually runs would stop working for a
/// reason no surface could explain. It is the twin of `IsBoundedAddress` in
/// `core_model_effect_validation.cc`: the one thing a party with no register
/// in reach can say about a person's own address.
pub(super) fn user_base_url_of(endpoint: &str) -> Option<String> {
    if endpoint.is_empty() {
        return None;
    }
    Some(endpoint.to_owned())
}

/// Where a direct call goes, and which rule the browser is to judge it by.
///
/// The pair is built in one place because the two halves are one decision. An
/// address composed under one rule and announced under the other is refused by
/// the rule it claimed, which is right — but it is a refusal nobody could have
/// intended, so there is no arrangement of this function that can produce it.
pub(super) fn direct_endpoint(candidate: &RouteCandidate) -> Result<Destination, ModelTurnError> {
    match candidate.endpoint_layer {
        // A published document named this address, so the browser holds it
        // too and compares the two.
        CatalogLayer::EmbeddedBaseline | CatalogLayer::RemoteOverlay => {
            origin_of(candidate.endpoint.as_str())
                .map(|origin| (origin, core_service_types::ModelEndpointKind::CatalogOrigin))
                .ok_or(ModelTurnError::EndpointUnusable)
        }
        // The person named this one, and the browser recognizes it rather
        // than judging it.
        CatalogLayer::UserOverride => user_base_url_of(candidate.endpoint.as_str())
            .map(|base| (base, core_service_types::ModelEndpointKind::UserBaseUrl))
            .ok_or(ModelTurnError::OwnEndpointUnusable),
    }
}

/// The worker origin a managed effect names, read off the plan's own
/// disclosure.
///
/// The disclosure is where the selector put the entitlement's worker host, and
/// reading it back keeps one source: an entitlement summary that never
/// produced a usable host produces no `EdgeWorker` recipient, and the compose
/// refuses here rather than naming an endpoint nobody stated.
pub(super) fn managed_endpoint(candidate: &RouteCandidate) -> Result<String, ModelTurnError> {
    let host = candidate
        .disclosure
        .egress
        .recipients
        .iter()
        .find_map(|recipient| match recipient {
            Recipient::EdgeWorker { host } => Some(host.as_str()),
            Recipient::Gateway { .. } | Recipient::Provider { .. } => None,
        })
        .ok_or(ModelTurnError::EndpointUnusable)?;
    origin_of(&format!("https://{host}")).ok_or(ModelTurnError::EndpointUnusable)
}

/// Refuses a composed turn whose bounded fields do not fit the contract.
///
/// The browser refuses the same shapes and would answer `INVALID_RESULT`; this
/// refuses first, so the reason names the composition rather than the
/// transport that carried it.
pub(super) fn validate(
    turn: ComposedModelTurn,
    call_id: &str,
) -> Result<ComposedModelTurn, ModelTurnError> {
    validate_request(&turn.request, call_id)?;
    for attempt in &turn.failover {
        validate_request(&attempt.request, call_id)?;
    }
    Ok(turn)
}

pub(super) fn validate_attempt(
    attempt: ComposedModelAttempt,
    call_id: &str,
) -> Result<ComposedModelAttempt, ModelTurnError> {
    validate_request(&attempt.request, call_id)?;
    Ok(attempt)
}

fn validate_request(
    request: &core_service_types::ModelRequestEffect,
    call_id: &str,
) -> Result<(), ModelTurnError> {
    let identifiers = [
        request.route_id.as_str(),
        request.model_id.as_str(),
        request.task_id.as_str(),
        call_id,
    ];
    if identifiers
        .into_iter()
        .any(|value| value.is_empty() || value.len() > core_service_types::MAX_IDENTIFIER_BYTES)
        || request.provider_id.is_empty()
        || request.provider_id.len() > core_service_types::MAX_PROVIDER_ID_BYTES
        || request.endpoint.len() > core_service_types::MAX_PROVIDER_ENDPOINT_BYTES
        || request.request_body.is_empty()
        || request.request_body.len() > core_service_types::MAX_EFFECT_BYTES
        || request.credential_handle.as_ref().is_some_and(|handle| {
            handle.is_empty() || handle.len() > core_service_types::MAX_IDENTIFIER_BYTES
        })
    {
        return Err(ModelTurnError::Overflow);
    }
    Ok(())
}
