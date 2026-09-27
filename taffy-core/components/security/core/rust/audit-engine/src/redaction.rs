// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The independent redaction pass (data and privacy sections 7.2 and 14).
//!
//! This is the last layer of the redaction pipeline and the only one that
//! assumes every layer before it failed. It does not trust the caller, it does
//! not trust the payload's own labelling, and it deliberately shares no code
//! with the projection serializers in `policy-engine`: "audit and telemetry
//! serializers independently redact; never reuse debug or model payload
//! serializers". The small amount of duplicated parsing below is the price of
//! that independence and is meant to be there.
//!
//! Four gates run, and a value has to pass all of them:
//!
//! 1. **the field policy** — every field name has a disposition per serializer,
//!    and a name with no policy is dropped;
//! 2. **kind agreement** — the field name declares the value kind it carries,
//!    and a value of another kind is dropped, so text cannot be smuggled
//!    through a field that is allowed to be emitted;
//! 3. **origin reduction** — a URL is reduced to its normalized origin before
//!    anything sees it, so a query string cannot carry a token onward;
//! 4. **the marker scan** — every string that survives the first three gates is
//!    checked for the words a secret is usually labelled with, and replaced if
//!    one is found.
//!
//! Nothing here can be relaxed by a parameter, and nothing takes a hint from
//! the event's redaction class about a specific field: the class is a ceiling
//! applied on top, never a permission.

use crate::payload::{FieldName, FieldValue};

/// How long an identifier may be before it stops looking like one.
///
/// An opaque domain identifier is short. A long one is something else wearing
/// an identifier's field, so it is dropped rather than truncated: truncating
/// would keep a prefix of whatever it really was.
const MAX_IDENTIFIER_CHARS: usize = 128;

/// The fixed marker a scrubbed value is replaced with.
const SCRUB_MARKER: &str = "[redacted]";

/// What a serializer does with one field.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FieldDisposition {
    /// Carry it, after the scan.
    Emit,
    /// Carry only the normalized origin of the URL it holds.
    OriginOnly,
    /// Do not carry it at all.
    Drop,
}

impl FieldDisposition {
    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Emit => "emit",
            Self::OriginOnly => "origin_only",
            Self::Drop => "drop",
        }
    }
}

/// What the product-audit serializer does with a field.
///
/// Audit answers what a task did for the user: which action was proposed,
/// whether approval happened, which data class crossed a boundary, and how the
/// result was verified. That needs identifiers, enumerated names, counts, and
/// an origin. It never needs page text, a prompt, a model output, or a file
/// name, so no field of that shape is emitted — there is no allowlist entry a
/// caller can reach for.
pub const fn audit_disposition(name: FieldName) -> FieldDisposition {
    match name {
        FieldName::TaskId
        | FieldName::ActionId
        | FieldName::CapabilityId
        | FieldName::ActorLeaseId
        | FieldName::ApprovalId
        | FieldName::NodeId
        | FieldName::PageEpoch
        | FieldName::ModelId
        | FieldName::GraphRevision
        | FieldName::ActionClass
        | FieldName::ResultCode
        | FieldName::DecisionReason
        | FieldName::DecisionStep
        | FieldName::ControlMode
        | FieldName::ApprovalDecision
        | FieldName::VerifierKind
        | FieldName::Sensitivity
        | FieldName::DataClass
        | FieldName::ProviderRoute
        | FieldName::PolicyVersion
        | FieldName::SourceCount
        | FieldName::ByteCount
        | FieldName::LatencyBucket
        | FieldName::BudgetExceeded
        | FieldName::ContentValuesRetained
        | FieldName::ErrorCode => FieldDisposition::Emit,
        FieldName::OriginNormalized | FieldName::DestinationUrl => FieldDisposition::OriginOnly,
        FieldName::UserVisibleSummary
        | FieldName::PageTitle
        | FieldName::PromptText
        | FieldName::ModelOutput
        | FieldName::FileName
        | FieldName::SelectedText => FieldDisposition::Drop,
    }
}

/// What the operational-telemetry serializer does with a field.
///
/// Telemetry answers whether a component crashed, timed out, truncated,
/// rejected stale state, or exceeded a budget. Enumerated names, counts, and
/// flags answer all of that. Every identifier is dropped — including the model
/// identifier, which the allowlist would permit but which arrives as a
/// caller-supplied string, and a caller-supplied string is exactly what a
/// content-free record must not contain.
pub const fn telemetry_disposition(name: FieldName) -> FieldDisposition {
    match name {
        FieldName::ActionClass
        | FieldName::ResultCode
        | FieldName::DecisionReason
        | FieldName::DecisionStep
        | FieldName::ControlMode
        | FieldName::ApprovalDecision
        | FieldName::VerifierKind
        | FieldName::Sensitivity
        | FieldName::DataClass
        | FieldName::ProviderRoute
        | FieldName::ErrorCode
        | FieldName::PolicyVersion
        | FieldName::SourceCount
        | FieldName::ByteCount
        | FieldName::LatencyBucket
        | FieldName::BudgetExceeded
        | FieldName::ContentValuesRetained => FieldDisposition::Emit,
        FieldName::TaskId
        | FieldName::ActionId
        | FieldName::CapabilityId
        | FieldName::ActorLeaseId
        | FieldName::ApprovalId
        | FieldName::NodeId
        | FieldName::PageEpoch
        | FieldName::ModelId
        | FieldName::GraphRevision
        | FieldName::OriginNormalized
        | FieldName::DestinationUrl
        | FieldName::UserVisibleSummary
        | FieldName::PageTitle
        | FieldName::PromptText
        | FieldName::ModelOutput
        | FieldName::FileName
        | FieldName::SelectedText => FieldDisposition::Drop,
    }
}

/// Whether the field's value is the kind its name declares.
///
/// A mismatch is a dropped field. The caller may have meant well; the record
/// still does not carry a value nobody wrote a policy for.
pub fn kinds_agree(name: FieldName, value: &FieldValue) -> bool {
    name.expected_kind() == value.kind()
}

/// The words a secret is usually labelled with.
///
/// A short, compiled-in list. It is a last resort, not the primary control:
/// the field policy is what keeps content out, and this catches a value that
/// arrived through a field that was otherwise allowed.
const SECRET_MARKERS: &[&str] = &[
    "password",
    "passcode",
    "passphrase",
    "secret",
    "token",
    "apikey",
    "api_key",
    "api-key",
    "cvv",
    "cvc",
    "otp",
    "one-time",
    "onetime",
    "seedphrase",
    "seed-phrase",
    "privatekey",
    "private_key",
    "private-key",
    "recoverycode",
    "recovery-code",
    "sessionid",
    "session-id",
    "bearer",
    "credential",
];

/// Whether a string names itself as a secret.
pub fn contains_secret_marker(text: &str) -> bool {
    let lowered = text.to_ascii_lowercase();
    SECRET_MARKERS.iter().any(|marker| lowered.contains(marker))
}

/// What the scan did to one string.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Scrubbed {
    /// The value passed and is carried as it arrived.
    Kept(String),
    /// The value was replaced. The record says a value was there and does not
    /// say what it was.
    Replaced,
    /// The value was dropped entirely.
    Dropped,
}

impl Scrubbed {
    /// The string to record, when one is recorded at all.
    pub fn value(&self) -> Option<&str> {
        match self {
            Self::Kept(value) => Some(value),
            Self::Replaced => Some(SCRUB_MARKER),
            Self::Dropped => None,
        }
    }

    /// Whether anything survived.
    pub fn is_dropped(&self) -> bool {
        matches!(self, Self::Dropped)
    }
}

/// How many separator characters an opaque identifier may contain.
///
/// One prefix separator is ordinary — `task_7f3a2b` names its namespace. More
/// than that is structure, and structure is not what an opaque identifier has.
const MAX_IDENTIFIER_SEPARATORS: usize = 1;

/// How many alphanumeric characters make a value long enough to be worth a
/// secret.
const SECRET_LENGTH_THRESHOLD: usize = 16;

/// Runs the scan over an opaque identifier.
///
/// Three rules, and the last two are the ones that do not trust the caller:
///
/// - an identifier too long to be one is dropped, rather than truncated, because
///   truncating keeps a prefix of whatever it really was;
/// - one that names itself a secret is replaced;
/// - one that is *structured* — long, mixing letters and digits, and carrying
///   more separators than a namespace prefix needs — is replaced, because an
///   opaque identifier is one token and a value with internal structure is
///   something else wearing an identifier's field.
///
/// The cost of the third rule is a replaced correlation identifier in an audit
/// record, which is visible. The cost of not having it is a secret in one,
/// which is not.
pub fn scrub_identifier(value: &str) -> Scrubbed {
    if value.is_empty() || value.chars().count() > MAX_IDENTIFIER_CHARS {
        return Scrubbed::Dropped;
    }
    if contains_secret_marker(value) || is_structured_value(value) {
        return Scrubbed::Replaced;
    }
    Scrubbed::Kept(value.to_owned())
}

/// Projects an identifier carried by a required audit field.
///
/// Required fields keep their shape when their value cannot survive the
/// identifier scrubber. This is the canonical projection used both while
/// encoding an audit record and while independently validating that record.
pub fn required_identifier(value: &str) -> String {
    match scrub_identifier(value) {
        Scrubbed::Kept(value) => value,
        Scrubbed::Replaced | Scrubbed::Dropped => SCRUB_MARKER.to_owned(),
    }
}

/// Projects an identifier carried by an optional audit field.
///
/// A suspicious value remains visibly present as the scrub marker; an invalid
/// empty or overlong value is absent.
pub fn optional_identifier(value: &str) -> Option<String> {
    match scrub_identifier(value) {
        Scrubbed::Kept(value) => Some(value),
        Scrubbed::Replaced => Some(SCRUB_MARKER.to_owned()),
        Scrubbed::Dropped => None,
    }
}

/// Whether a value has more internal structure than an opaque identifier has.
fn is_structured_value(value: &str) -> bool {
    let alphanumeric = value.chars().filter(char::is_ascii_alphanumeric).count();
    if alphanumeric < SECRET_LENGTH_THRESHOLD {
        return false;
    }
    let separators = value
        .chars()
        .filter(|character| !character.is_ascii_alphanumeric())
        .count();
    separators > MAX_IDENTIFIER_SEPARATORS
        && value
            .chars()
            .any(|character| character.is_ascii_alphabetic())
        && value.chars().any(|character| character.is_ascii_digit())
}

/// Reduces a URL to its normalized origin.
///
/// Deliberately a separate, smaller parser from the one in `policy-engine`: an
/// independent layer that shares its implementation with the layer it is
/// checking is not independent. It understands `scheme://host[:port]` and
/// discards everything from the first `/`, `?`, or `#` onward, so a path, a
/// query, and a fragment cannot survive whatever they contain.
pub fn origin_of(url: &str) -> Option<String> {
    let (scheme, rest) = url.split_once("://")?;
    if !valid_scheme(scheme) {
        return None;
    }
    let authority = rest
        .split(['/', '?', '#'])
        .next()
        .filter(|value| !value.is_empty())?;
    if authority.contains('@') {
        // Credentials in an authority are not an origin and are never recorded.
        return None;
    }
    let (host, port) = split_host_and_port(authority)?;

    let scheme = scheme.to_ascii_lowercase();
    let host = normalize_host(host)?;
    let port = match port {
        None => None,
        Some(text) => {
            let parsed: u16 = text.parse().ok()?;
            if default_port(&scheme) == Some(parsed) {
                None
            } else {
                Some(parsed)
            }
        }
    };

    Some(match (host.contains(':'), port) {
        (true, Some(port)) => format!("{scheme}://[{host}]:{port}"),
        (true, None) => format!("{scheme}://[{host}]"),
        (false, Some(port)) => format!("{scheme}://{host}:{port}"),
        (false, None) => format!("{scheme}://{host}"),
    })
}

/// Whether `scheme` has the RFC 3986 shape accepted at this boundary.
fn valid_scheme(scheme: &str) -> bool {
    let mut bytes = scheme.bytes();
    matches!(bytes.next(), Some(first) if first.is_ascii_alphabetic())
        && bytes.all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'+' | b'-' | b'.'))
}

/// Normalizes one host, rejecting text that cannot be a browser origin host.
fn normalize_host(host: &str) -> Option<String> {
    if host.contains(':') {
        return host
            .parse::<std::net::Ipv6Addr>()
            .ok()
            .map(|address| address.to_string());
    }
    if host.is_empty() || host.len() > 253 || !host.is_ascii() {
        return None;
    }
    if host
        .bytes()
        .all(|byte| byte.is_ascii_digit() || byte == b'.')
    {
        return host
            .parse::<std::net::Ipv4Addr>()
            .ok()
            .map(|address| address.to_string());
    }
    if !host.split('.').all(valid_domain_label) {
        return None;
    }
    Some(host.to_ascii_lowercase())
}

/// Whether one ASCII DNS label is syntactically valid.
fn valid_domain_label(label: &str) -> bool {
    if label.is_empty() || label.len() > 63 {
        return false;
    }
    let mut bytes = label.bytes();
    let Some(first) = bytes.next() else {
        return false;
    };
    let Some(last) = label.bytes().last() else {
        return false;
    };
    first.is_ascii_alphanumeric()
        && last.is_ascii_alphanumeric()
        && bytes.all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
}

/// The default port for a scheme, when it has one.
const fn default_port(scheme: &str) -> Option<u16> {
    match scheme.as_bytes() {
        b"https" | b"wss" => Some(443),
        b"http" | b"ws" => Some(80),
        _ => None,
    }
}

/// Splits `host[:port]`, keeping a bracketed address literal intact.
fn split_host_and_port(authority: &str) -> Option<(&str, Option<&str>)> {
    if let Some(after_bracket) = authority.strip_prefix('[') {
        let (host, tail) = after_bracket.split_once(']')?;
        if tail.is_empty() {
            return Some((host, None));
        }
        return Some((host, Some(tail.strip_prefix(':')?)));
    }
    match authority.split_once(':') {
        None => Some((authority, None)),
        Some((host, port)) if !port.contains(':') => Some((host, Some(port))),
        Some(_) => None,
    }
}

#[cfg(test)]
mod tests {
    use super::{
        audit_disposition, contains_secret_marker, kinds_agree, origin_of, scrub_identifier,
        telemetry_disposition, FieldDisposition, Scrubbed,
    };
    use crate::payload::{FieldName, FieldValue};

    #[test]
    fn telemetry_is_never_wider_than_audit() {
        for name in FieldName::ALL {
            let audit = audit_disposition(*name);
            let telemetry = telemetry_disposition(*name);
            if telemetry == FieldDisposition::Emit {
                assert_eq!(
                    audit,
                    FieldDisposition::Emit,
                    "{} reaches telemetry but not audit",
                    name.label()
                );
            }
        }
    }

    #[test]
    fn no_text_field_reaches_either_record() {
        for name in FieldName::ALL {
            if name.expected_kind() == crate::payload::ValueKind::Text {
                assert_eq!(audit_disposition(*name), FieldDisposition::Drop);
                assert_eq!(telemetry_disposition(*name), FieldDisposition::Drop);
            }
        }
    }

    #[test]
    fn a_value_of_the_wrong_kind_does_not_agree_with_its_field() {
        assert!(kinds_agree(
            FieldName::ActionClass,
            &FieldValue::Enumerated("open_link")
        ));
        assert!(!kinds_agree(
            FieldName::ActionClass,
            &FieldValue::Text("open_link".to_owned())
        ));
        assert!(!kinds_agree(
            FieldName::TaskId,
            &FieldValue::Url("https://example.test".to_owned())
        ));
    }

    #[test]
    fn an_identifier_that_names_itself_a_secret_is_replaced() {
        assert_eq!(
            scrub_identifier("task_7f3a2b"),
            Scrubbed::Kept("task_7f3a2b".to_owned())
        );
        assert_eq!(scrub_identifier("session-token-9f2b"), Scrubbed::Replaced);
        assert_eq!(scrub_identifier(""), Scrubbed::Dropped);
        assert_eq!(scrub_identifier(&"x".repeat(200)), Scrubbed::Dropped);
        assert!(contains_secret_marker("CANARY-PASSWORD-1"));
    }

    #[test]
    fn a_structured_value_wearing_an_identifiers_field_is_replaced() {
        // Nothing in it names a secret. It is still not one opaque token.
        assert_eq!(
            scrub_identifier("TAFFYGO-CANARY-FRAME-8C2D57"),
            Scrubbed::Replaced
        );
        assert_eq!(
            scrub_identifier("ABCD-EFGH-1234-WXYZ-5678"),
            Scrubbed::Replaced
        );

        // The identifiers this system actually mints are one token, optionally
        // with a namespace prefix.
        assert_eq!(
            scrub_identifier("cap_0f2b7c19a4e35d6081bc"),
            Scrubbed::Kept("cap_0f2b7c19a4e35d6081bc".to_owned())
        );
        assert_eq!(
            scrub_identifier("0f2b7c19a4e35d6081bc4a7e"),
            Scrubbed::Kept("0f2b7c19a4e35d6081bc4a7e".to_owned())
        );
    }

    #[test]
    fn a_url_is_reduced_to_its_origin() {
        assert_eq!(
            origin_of("https://Example.test:443/orders/42?token=abc#top").as_deref(),
            Some("https://example.test")
        );
        assert_eq!(
            origin_of("http://example.test:8080/x").as_deref(),
            Some("http://example.test:8080")
        );
        assert_eq!(origin_of("not a url"), None);
        assert_eq!(origin_of("https://user:pw@example.test/"), None);
        assert_eq!(origin_of("1https://example.test/"), None);
        assert_eq!(origin_of("https://not a host.example/"), None);
        assert_eq!(
            origin_of("https://[2001:db8::1]:443/path").as_deref(),
            Some("https://[2001:db8::1]")
        );
    }
}
