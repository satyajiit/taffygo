// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Transient validation of fixed-route account token responses.

use std::collections::{BTreeMap, BTreeSet};

use core_service_types as wire;
use model_router::json::{self, JsonValue};

const TOP_LEVEL_FIELDS: &[&str] = &[
    "access_token",
    "expires_at",
    "expires_in",
    "id_token",
    "provider_refresh_token",
    "provider_token",
    "refresh_token",
    "token_type",
    "user",
    "weak_password",
];

const USER_FIELDS: &[&str] = &[
    "app_metadata",
    "aud",
    "banned_until",
    "confirmed_at",
    "confirmation_sent_at",
    "created_at",
    "deleted_at",
    "email",
    "email_change_confirm_status",
    "email_change_sent_at",
    "email_confirmed_at",
    "factors",
    "id",
    "identities",
    "invited_at",
    "is_anonymous",
    "last_sign_in_at",
    "new_email",
    "new_phone",
    "phone",
    "phone_change_sent_at",
    "phone_confirmed_at",
    "reauthentication_sent_at",
    "recovery_sent_at",
    "role",
    "updated_at",
    "user_metadata",
];

/// Validates one already-completed account-plane 2xx response without touching
/// the account reducer or durable state.
pub fn validate_account_token_response(
    mut request: wire::AccountTokenValidationRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
) -> wire::AccountTokenValidationResult {
    let mut result = empty_result(&request);
    if request.operation.service_generation != expected_generation {
        result.status = wire::AccountTokenValidationStatus::StaleGeneration;
        clear_bytes(&mut request.response_body);
        return result;
    }
    if request.operation.deadline_monotonic_ms <= now_monotonic_ms {
        result.status = wire::AccountTokenValidationStatus::DeadlineExceeded;
        clear_bytes(&mut request.response_body);
        return result;
    }
    if !valid_operation(&request) {
        clear_bytes(&mut request.response_body);
        return result;
    }
    if request.response_body.is_empty()
        || request.response_body.len() > wire::MAX_ACCOUNT_RESPONSE_BYTES
    {
        result.status = wire::AccountTokenValidationStatus::ResourceLimit;
        clear_bytes(&mut request.response_body);
        return result;
    }

    let parsed = core::str::from_utf8(&request.response_body)
        .ok()
        .and_then(|text| json::parse(text).ok());
    clear_bytes(&mut request.response_body);
    let Some(JsonValue::Object(mut root)) = parsed else {
        return result;
    };
    if !has_only_fields(&root, TOP_LEVEL_FIELDS) {
        clear_json_map(&mut root);
        return result;
    }

    let access_token = take_text(&mut root, "access_token");
    let refresh_token = take_text(&mut root, "refresh_token");
    let token_type = take_text(&mut root, "token_type");
    let expires_in = root.remove("expires_in").and_then(|value| value.as_i64());
    let Some(JsonValue::Object(mut user)) = root.remove("user") else {
        clear_optional_string(access_token);
        clear_optional_string(refresh_token);
        clear_optional_string(token_type);
        clear_json_map(&mut root);
        return result;
    };

    let validated = validate_fields(
        &request,
        access_token.as_deref(),
        refresh_token.as_deref(),
        token_type.as_deref(),
        expires_in,
        &mut user,
        &root,
    );
    let Some((account_subject, expires_in_seconds, identity)) = validated else {
        clear_optional_string(access_token);
        clear_optional_string(refresh_token);
        clear_optional_string(token_type);
        clear_json_map(&mut user);
        clear_json_map(&mut root);
        return result;
    };

    let mut access_token = access_token.unwrap_or_default();
    let mut refresh_token = refresh_token.unwrap_or_default();
    result.status = wire::AccountTokenValidationStatus::Validated;
    result.account_subject = account_subject;
    result.expires_in_seconds = expires_in_seconds;
    result.access_token = core::mem::take(&mut access_token).into_bytes();
    result.refresh_token = core::mem::take(&mut refresh_token).into_bytes();
    result.email = identity.email;
    result.display_name = identity.display_name;
    clear_optional_string(token_type);
    clear_json_map(&mut user);
    clear_json_map(&mut root);
    result
}

fn empty_result(
    request: &wire::AccountTokenValidationRequest,
) -> wire::AccountTokenValidationResult {
    wire::AccountTokenValidationResult {
        status: wire::AccountTokenValidationStatus::InvalidResponse,
        operation_id: request.operation.operation_id.clone(),
        operation_kind: request.operation_kind,
        auth_method: request.expected_auth_method,
        account_subject: String::new(),
        expires_in_seconds: 0,
        target_rotation: request.target_rotation,
        access_token: Vec::new(),
        refresh_token: Vec::new(),
        email: None,
        display_name: None,
    }
}

fn valid_operation(request: &wire::AccountTokenValidationRequest) -> bool {
    let operation = &request.operation;
    if operation.service_generation == 0
        || operation.task_revision != 0
        || !valid_correlation(&operation.operation_id, wire::MAX_OPERATION_ID_BYTES)
        || !valid_correlation(&operation.idempotency_key, wire::MAX_IDEMPOTENCY_KEY_BYTES)
    {
        return false;
    }
    match request.operation_kind {
        wire::AccountNetworkOperation::ExchangeAuthorizationCode
        | wire::AccountNetworkOperation::ExchangeNativeCredential => {
            request.target_rotation == 0 && request.expected_account_subject.is_none()
        }
        wire::AccountNetworkOperation::RefreshSession => {
            request.target_rotation > 0
                && request
                    .expected_account_subject
                    .as_ref()
                    .is_some_and(|subject| valid_subject(subject))
        }
        wire::AccountNetworkOperation::RequestEmailLink
        | wire::AccountNetworkOperation::RevokeSession
        | wire::AccountNetworkOperation::FetchEntitlement => false,
    }
}

fn validate_fields(
    request: &wire::AccountTokenValidationRequest,
    access_token: Option<&str>,
    refresh_token: Option<&str>,
    token_type: Option<&str>,
    expires_in: Option<i64>,
    user: &mut BTreeMap<String, JsonValue>,
    remaining_root: &BTreeMap<String, JsonValue>,
) -> Option<(String, u64, AccountIdentity)> {
    if !has_only_fields(user, USER_FIELDS)
        || !access_token
            .is_some_and(|value| valid_token(value, wire::MAX_ACCOUNT_ACCESS_TOKEN_BYTES))
        || !refresh_token
            .is_some_and(|value| valid_token(value, wire::MAX_ACCOUNT_REFRESH_TOKEN_BYTES))
        || token_type != Some("bearer")
    {
        return None;
    }
    let expires_in = u64::try_from(expires_in?).ok()?;
    if expires_in == 0 || expires_in > wire::MAX_ACCOUNT_SESSION_LIFETIME_SECONDS as u64 {
        return None;
    }
    if let Some(expires_at) = remaining_root.get("expires_at") {
        if expires_at.as_i64().is_none_or(|value| value <= 0) {
            return None;
        }
    }
    for name in ["provider_token", "provider_refresh_token"] {
        if let Some(value) = remaining_root.get(name) {
            let value = value.as_str()?;
            if !valid_token(value, wire::MAX_ACCOUNT_ACCESS_TOKEN_BYTES) {
                return None;
            }
        }
    }
    if let Some(value) = remaining_root.get("id_token") {
        let value = value.as_str()?;
        if !valid_token(value, wire::MAX_ACCOUNT_ID_TOKEN_BYTES) {
            return None;
        }
    }

    let subject = user.get("id")?.as_str()?.to_owned();
    if !valid_subject(&subject)
        || request
            .expected_account_subject
            .as_ref()
            .is_some_and(|expected| expected != &subject)
    {
        return None;
    }
    let metadata = user.get("app_metadata")?.as_object()?;
    let signup_provider = metadata.get("provider")?.as_str()?;
    if !valid_account_provider(signup_provider) {
        return None;
    }
    let providers = metadata.get("providers")?.as_array()?;
    if providers.is_empty() || providers.len() > wire::MAX_ACCOUNT_LINKED_PROVIDERS {
        return None;
    }
    let mut unique = BTreeSet::new();
    for value in providers {
        let value = value.as_str()?;
        if !valid_account_provider(value) || !unique.insert(value) {
            return None;
        }
    }
    if !unique.contains(signup_provider)
        || !unique.contains(expected_provider(request.expected_auth_method))
    {
        return None;
    }
    Some((subject, expires_in, read_identity(user)))
}

/// What the account plane says this account is called, when it says anything.
///
/// Both halves are optional and neither can fail the validation. A response
/// that establishes a session is a valid response whether or not it carries a
/// name, and refusing a sign-in over a malformed label would trade a working
/// account for a cosmetic field.
struct AccountIdentity {
    email: Option<String>,
    display_name: Option<String>,
}

/// Reads the two label fields, taking the first shape that is actually a label.
///
/// The address is `user.email`, which the account plane sets and a client
/// cannot. The name is not: `user_metadata` is writable by the signed-in person
/// through the account plane's own API, so this value is self-asserted. That is
/// acceptable for what it is used for — it is shown to the same person who set
/// it and to nobody else — and it is why the value is bounded here, is never
/// compared, and never reaches a decision.
///
/// Four keys are read because four providers spell it differently and the
/// account plane passes each through untouched: `full_name` is what Google and
/// Facebook write, `name` is the OIDC claim, `user_name` is what GitHub writes
/// and `preferred_username` is the OIDC spelling of the same thing. They are
/// tried in that order, so a real name wins over a handle where both exist.
fn read_identity(user: &BTreeMap<String, JsonValue>) -> AccountIdentity {
    let email = user
        .get("email")
        .and_then(JsonValue::as_str)
        .filter(|value| bounded_label(value, wire::MAX_ACCOUNT_EMAIL_BYTES))
        .map(str::to_owned);
    let metadata = user.get("user_metadata").and_then(JsonValue::as_object);
    let display_name = metadata
        .and_then(|metadata| {
            ["full_name", "name", "user_name", "preferred_username"]
                .into_iter()
                .find_map(|key| {
                    metadata
                        .get(key)
                        .and_then(JsonValue::as_str)
                        .filter(|value| bounded_label(value, wire::MAX_ACCOUNT_DISPLAY_NAME_BYTES))
                })
        })
        .map(str::to_owned);
    AccountIdentity {
        email,
        display_name,
    }
}

/// Whether provider-supplied text is worth passing on as a label at all.
///
/// Deliberately the same three refusals the domain types apply, so a value that
/// survives here is one the decode on the far side will also accept. The two
/// checks are not one check: this one drops a bad label and keeps the session,
/// while the decode fails the whole message, because by then the browser has
/// asserted the value rather than merely relayed it.
fn bounded_label(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && !value.chars().any(char::is_control)
        && !value.trim().is_empty()
}

fn valid_account_provider(value: &str) -> bool {
    matches!(value, "email" | "google" | "github" | "facebook")
}

const fn expected_provider(method: wire::AccountAuthMethod) -> &'static str {
    match method {
        wire::AccountAuthMethod::Google => "google",
        wire::AccountAuthMethod::EmailLink => "email",
        wire::AccountAuthMethod::Github => "github",
        wire::AccountAuthMethod::Facebook => "facebook",
    }
}

fn has_only_fields(value: &BTreeMap<String, JsonValue>, allowed: &[&str]) -> bool {
    value.keys().all(|key| allowed.contains(&key.as_str()))
}

fn valid_subject(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= wire::MAX_IDENTIFIER_BYTES
        && value.bytes().all(|byte| byte.is_ascii_graphic())
}

fn valid_correlation(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && value.bytes().all(|byte| byte.is_ascii_graphic())
}

fn valid_token(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && value.bytes().all(|byte| byte.is_ascii_graphic())
}

fn take_text(value: &mut BTreeMap<String, JsonValue>, name: &str) -> Option<String> {
    match value.remove(name) {
        Some(JsonValue::Text(text)) => Some(text),
        _ => None,
    }
}

fn clear_bytes(value: &mut [u8]) {
    value.fill(0);
}

fn clear_optional_string(value: Option<String>) {
    if let Some(value) = value {
        clear_string(value);
    }
}

fn clear_json_map(value: &mut BTreeMap<String, JsonValue>) {
    for (key, mut nested) in core::mem::take(value) {
        clear_string(key);
        clear_json(&mut nested);
    }
}

fn clear_json(value: &mut JsonValue) {
    match value {
        JsonValue::Text(text) | JsonValue::Decimal(text) => clear_string(core::mem::take(text)),
        JsonValue::Array(values) => values.iter_mut().for_each(clear_json),
        JsonValue::Object(values) => clear_json_map(values),
        JsonValue::Null | JsonValue::Bool(_) | JsonValue::Integer(_) => {}
    }
}

fn clear_string(value: String) {
    let mut bytes = value.into_bytes();
    bytes.fill(0);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request(body: &str) -> wire::AccountTokenValidationRequest {
        wire::AccountTokenValidationRequest {
            operation: wire::OperationEnvelope {
                operation_id: "operation-1".to_owned(),
                service_generation: 7,
                task_revision: 0,
                deadline_monotonic_ms: 10_000,
                idempotency_key: "token-validation-1".to_owned(),
            },
            operation_kind: wire::AccountNetworkOperation::ExchangeAuthorizationCode,
            expected_auth_method: wire::AccountAuthMethod::Github,
            expected_account_subject: None,
            target_rotation: 0,
            response_body: body.as_bytes().to_vec(),
        }
    }

    fn valid_body() -> &'static str {
        r#"{"access_token":"access-value","refresh_token":"refresh-value","token_type":"bearer","expires_in":3600,"user":{"id":"subject-1","app_metadata":{"provider":"github","providers":["github"]}}}"#
    }

    fn body_with(access: &str, expires_in: u64, providers: &str, extra: &str) -> String {
        format!(
            "{{\"access_token\":\"{access}\",\"refresh_token\":\"refresh-value\",\"token_type\":\"bearer\",\"expires_in\":{expires_in}{extra},\"user\":{{\"id\":\"subject-1\",\"app_metadata\":{{\"provider\":\"github\",\"providers\":{providers}}}}}}}"
        )
    }

    #[test]
    fn validates_exact_method_subject_tokens_and_expiry() {
        let result = validate_account_token_response(request(valid_body()), 7, 100);
        assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
        assert_eq!(result.operation_id, "operation-1");
        assert_eq!(result.account_subject, "subject-1");
        assert_eq!(result.expires_in_seconds, 3600);
        assert_eq!(result.target_rotation, 0);
        assert_eq!(result.access_token, b"access-value");
        assert_eq!(result.refresh_token, b"refresh-value");
    }

    #[test]
    fn carries_the_address_and_the_name_the_provider_supplied() {
        let body = valid_body().replace(
            r#""id":"subject-1""#,
            r#""id":"subject-1","email":"reader@example.test","user_metadata":{"full_name":"A Reader"}"#,
        );
        let result = validate_account_token_response(request(&body), 7, 100);
        assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
        assert_eq!(result.email.as_deref(), Some("reader@example.test"));
        assert_eq!(result.display_name.as_deref(), Some("A Reader"));
    }

    #[test]
    fn a_response_with_no_identity_still_establishes_a_session() {
        // The four providers do not all supply a name, and one that does not
        // is not a failed sign-in. Absent has to stay different from refused.
        let result = validate_account_token_response(request(valid_body()), 7, 100);
        assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
        assert!(result.email.is_none());
        assert!(result.display_name.is_none());
    }

    #[test]
    fn the_name_is_read_from_whichever_key_the_provider_spells_it_with() {
        // Four providers, four spellings, and the account plane passes each
        // through untouched. Real name before handle where both are present.
        for (metadata, expected) in [
            (r#"{"full_name":"A Reader"}"#, "A Reader"),
            (r#"{"name":"A Reader"}"#, "A Reader"),
            (r#"{"user_name":"reader"}"#, "reader"),
            (r#"{"preferred_username":"reader"}"#, "reader"),
            (
                r#"{"full_name":"A Reader","user_name":"reader"}"#,
                "A Reader",
            ),
        ] {
            let body = valid_body().replace(
                r#""id":"subject-1""#,
                &format!(r#""id":"subject-1","user_metadata":{metadata}"#),
            );
            let result = validate_account_token_response(request(&body), 7, 100);
            assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
            assert_eq!(result.display_name.as_deref(), Some(expected), "{metadata}");
        }
    }

    #[test]
    fn a_label_that_is_not_a_label_is_dropped_rather_than_failing_the_sign_in() {
        // user_metadata is writable by the signed-in person through the account
        // plane's own API, so these are the shapes an account can be made to
        // carry. None of them may cost that account its session, and none may
        // reach the screen.
        let oversized = "n".repeat(wire::MAX_ACCOUNT_DISPLAY_NAME_BYTES + 1);
        for metadata in [
            r#"{"full_name":""}"#.to_owned(),
            r#"{"full_name":"   "}"#.to_owned(),
            r#"{"full_name":"two\nlines"}"#.to_owned(),
            r#"{"full_name":42}"#.to_owned(),
            format!(r#"{{"full_name":"{oversized}"}}"#),
        ] {
            let body = valid_body().replace(
                r#""id":"subject-1""#,
                &format!(r#""id":"subject-1","user_metadata":{metadata}"#),
            );
            let result = validate_account_token_response(request(&body), 7, 100);
            assert_eq!(
                result.status,
                wire::AccountTokenValidationStatus::Validated,
                "{metadata}"
            );
            assert!(result.display_name.is_none(), "{metadata}");
        }
    }

    #[test]
    fn an_oversized_address_is_dropped_and_a_name_beside_it_is_not() {
        let oversized = format!("{}@example.test", "a".repeat(wire::MAX_ACCOUNT_EMAIL_BYTES));
        let body = valid_body().replace(
            r#""id":"subject-1""#,
            &format!(
                r#""id":"subject-1","email":"{oversized}","user_metadata":{{"full_name":"A Reader"}}"#
            ),
        );
        let result = validate_account_token_response(request(&body), 7, 100);
        assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
        assert!(result.email.is_none());
        assert_eq!(result.display_name.as_deref(), Some("A Reader"));
    }

    #[test]
    fn rejects_unknown_fields_method_mismatch_and_duplicate_keys() {
        for body in [
            valid_body().replace("\"user\"", "\"unexpected\":1,\"user\""),
            valid_body().replace("\"github\"", "\"google\""),
            valid_body().replace(
                "\"expires_in\":3600",
                "\"expires_in\":3600,\"expires_in\":7200",
            ),
        ] {
            let result = validate_account_token_response(request(&body), 7, 100);
            assert_eq!(
                result.status,
                wire::AccountTokenValidationStatus::InvalidResponse
            );
            assert!(result.access_token.is_empty());
            assert!(result.refresh_token.is_empty());
        }
    }

    #[test]
    fn refresh_binds_subject_and_target_rotation() {
        let mut value = request(valid_body());
        value.operation_kind = wire::AccountNetworkOperation::RefreshSession;
        value.expected_account_subject = Some("subject-1".to_owned());
        value.target_rotation = 4;
        let result = validate_account_token_response(value, 7, 100);
        assert_eq!(result.status, wire::AccountTokenValidationStatus::Validated);
        assert_eq!(result.target_rotation, 4);

        let mut mismatch = request(valid_body());
        mismatch.operation_kind = wire::AccountNetworkOperation::RefreshSession;
        mismatch.expected_account_subject = Some("subject-2".to_owned());
        mismatch.target_rotation = 4;
        assert_eq!(
            validate_account_token_response(mismatch, 7, 100).status,
            wire::AccountTokenValidationStatus::InvalidResponse
        );
    }

    #[test]
    fn accepts_bounded_id_token_and_all_closed_linked_providers() {
        let body = body_with(
            "access-value",
            3600,
            r#"["email","google","github","facebook"]"#,
            ",\"id_token\":\"bounded-id-token\"",
        );
        assert_eq!(
            validate_account_token_response(request(&body), 7, 100).status,
            wire::AccountTokenValidationStatus::Validated
        );

        for providers in [
            r#"["github","github"]"#,
            r#"["github","unreviewed"]"#,
            r#"["email","google","facebook"]"#,
        ] {
            let body = body_with("access-value", 3600, providers, "");
            assert_eq!(
                validate_account_token_response(request(&body), 7, 100).status,
                wire::AccountTokenValidationStatus::InvalidResponse
            );
        }
    }

    #[test]
    fn accepts_a_linked_login_provider_when_the_signup_provider_differs() {
        let body = valid_body().replace(
            r#""provider":"github","providers":["github"]"#,
            r#""provider":"email","providers":["email","github"]"#,
        );
        assert_eq!(
            validate_account_token_response(request(&body), 7, 100).status,
            wire::AccountTokenValidationStatus::Validated
        );
    }

    #[test]
    fn linked_provider_metadata_remains_closed_and_complete() {
        for body in [
            valid_body().replace(r#","providers":["github"]"#, ""),
            valid_body().replace(
                r#""provider":"github","providers":["github"]"#,
                r#""provider":"unreviewed","providers":["unreviewed","github"]"#,
            ),
            valid_body().replace(
                r#""provider":"github","providers":["github"]"#,
                r#""provider":"email","providers":["github"]"#,
            ),
            valid_body().replace(r#"["github"]"#, r#"["github",1]"#),
        ] {
            assert_eq!(
                validate_account_token_response(request(&body), 7, 100).status,
                wire::AccountTokenValidationStatus::InvalidResponse
            );
        }
    }

    #[test]
    fn enforces_exact_token_and_expiry_bounds() {
        let exact_token = "a".repeat(wire::MAX_ACCOUNT_ACCESS_TOKEN_BYTES);
        let exact = body_with(
            &exact_token,
            wire::MAX_ACCOUNT_SESSION_LIFETIME_SECONDS as u64,
            r#"["github"]"#,
            "",
        );
        assert_eq!(
            validate_account_token_response(request(&exact), 7, 100).status,
            wire::AccountTokenValidationStatus::Validated
        );

        let oversized_token = "a".repeat(wire::MAX_ACCOUNT_ACCESS_TOKEN_BYTES + 1);
        for body in [
            body_with(&oversized_token, 3600, r#"["github"]"#, ""),
            body_with(
                "access-value",
                wire::MAX_ACCOUNT_SESSION_LIFETIME_SECONDS as u64 + 1,
                r#"["github"]"#,
                "",
            ),
        ] {
            assert_eq!(
                validate_account_token_response(request(&body), 7, 100).status,
                wire::AccountTokenValidationStatus::InvalidResponse
            );
        }
    }

    #[test]
    fn rejects_non_utf8_deep_and_non_token_operations() {
        let mut non_utf8 = request(valid_body());
        non_utf8.response_body = vec![0xff, 0xfe];
        assert_eq!(
            validate_account_token_response(non_utf8, 7, 100).status,
            wire::AccountTokenValidationStatus::InvalidResponse
        );

        let mut deep = request(&format!("{}0{}", "[".repeat(26), "]".repeat(26)));
        deep.operation_kind = wire::AccountNetworkOperation::ExchangeNativeCredential;
        assert_eq!(
            validate_account_token_response(deep, 7, 100).status,
            wire::AccountTokenValidationStatus::InvalidResponse
        );

        for operation_kind in [
            wire::AccountNetworkOperation::RequestEmailLink,
            wire::AccountNetworkOperation::RevokeSession,
        ] {
            let mut value = request(valid_body());
            value.operation_kind = operation_kind;
            assert_eq!(
                validate_account_token_response(value, 7, 100).status,
                wire::AccountTokenValidationStatus::InvalidResponse
            );
        }
    }

    #[test]
    fn rejects_invalid_operation_envelopes() {
        let mut invalid = request(valid_body());
        invalid.operation.service_generation = 0;
        assert_eq!(
            validate_account_token_response(invalid, 0, 100).status,
            wire::AccountTokenValidationStatus::InvalidResponse
        );
        let mut invalid = request(valid_body());
        invalid.operation.task_revision = 1;
        assert_eq!(
            validate_account_token_response(invalid, 7, 100).status,
            wire::AccountTokenValidationStatus::InvalidResponse
        );
    }

    #[test]
    fn refuses_stale_expired_and_oversized_requests() {
        assert_eq!(
            validate_account_token_response(request(valid_body()), 8, 100).status,
            wire::AccountTokenValidationStatus::StaleGeneration
        );
        assert_eq!(
            validate_account_token_response(request(valid_body()), 7, 10_000).status,
            wire::AccountTokenValidationStatus::DeadlineExceeded
        );
        let mut oversized = request(valid_body());
        oversized.response_body = vec![b'x'; wire::MAX_ACCOUNT_RESPONSE_BYTES + 1];
        assert_eq!(
            validate_account_token_response(oversized, 7, 100).status,
            wire::AccountTokenValidationStatus::ResourceLimit
        );
    }
}
