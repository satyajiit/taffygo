// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Origin normalization, redirect sets, and cross-origin frame eligibility
//! (protocol specification sections 5.5, 8.1, and 11.4).
//!
//! Three rules are encoded here rather than described.
//!
//! **An opaque origin stays opaque.** A sandboxed frame, a `data:` document, or
//! any other opaque origin is equal only to an opaque origin with the same
//! session-local identifier. It is never equal to its precursor, never matched
//! against a tuple allowlist entry, and never widened by any operation in this
//! module — [`NormalizedOrigin`] offers no accessor that yields a precursor,
//! because there is nothing safe to do with one.
//!
//! **Comparison happens on a normalized value or not at all.** Two strings for
//! the same origin must compare equal, so scheme and host case and the default
//! port are removed once, at the boundary, by [`normalize`]. A value that
//! cannot be normalized is refused; it is never compared as text.
//!
//! **A redirect set is explicit.** [`AllowedRedirects`] holds the exact origins
//! a navigation may land on, and an empty set forbids redirects rather than
//! permitting them. Nothing infers an allowance from a registrable domain, a
//! parent origin, or a scheme upgrade.

use core::fmt;

use bip_types::identity::{Origin, OriginKind};
use bip_types::snapshot::FrameExclusionReason;

/// Why an origin could not be normalized.
///
/// Every variant is a refusal to guess. None of them carries the rejected text:
/// the value came from a message boundary, and a diagnostic that quotes it
/// would carry page-derived content into logs.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum OriginError {
    /// A tuple origin carried no serialization.
    MissingSerialization,
    /// An opaque origin carried no session-local identifier.
    MissingOpaqueIdentifier,
    /// A tuple origin carried an opaque identifier, or the reverse. The two
    /// kinds do not mix.
    MismatchedFields,
    /// The serialization is not `scheme://host` with an optional port.
    MalformedSerialization,
    /// The serialization carried a path, query, fragment, or credentials. An
    /// origin is not a URL.
    NotAnOrigin,
    /// The port is not a number in range.
    MalformedPort,
}

impl fmt::Display for OriginError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::MissingSerialization => "a tuple origin needs a serialization",
            Self::MissingOpaqueIdentifier => "an opaque origin needs an identifier",
            Self::MismatchedFields => "an origin carried fields of both kinds",
            Self::MalformedSerialization => "an origin must be scheme://host with an optional port",
            Self::NotAnOrigin => "an origin carries no path, query, fragment, or credentials",
            Self::MalformedPort => "the port is not a number in range",
        };
        formatter.write_str(text)
    }
}

/// A security origin in the one form this crate compares.
///
/// Equality is structural, which is what makes the opaque rule hold without a
/// special case: an opaque origin has no scheme or host to match a tuple origin
/// against, so no comparison can broaden it.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum NormalizedOrigin {
    /// A scheme, host, and port. The port is absent when it is the scheme's
    /// default.
    Tuple {
        /// Lowercase scheme.
        scheme: String,
        /// Lowercase host.
        host: String,
        /// Port, absent when it is the default for the scheme.
        port: Option<u16>,
    },
    /// An opaque origin, identified only by a session-local value.
    Opaque {
        /// The session-local identifier. It is never a cross-site identifier
        /// and never survives the session.
        opaque_id: String,
    },
}

/// The default port for a scheme, when it has one.
const fn default_port(scheme: &str) -> Option<u16> {
    match scheme.as_bytes() {
        b"https" | b"wss" => Some(443),
        b"http" | b"ws" => Some(80),
        _ => None,
    }
}

impl NormalizedOrigin {
    /// Whether this origin is opaque.
    pub const fn is_opaque(&self) -> bool {
        matches!(self, Self::Opaque { .. })
    }

    /// The host, for a tuple origin.
    ///
    /// `None` for an opaque origin, which has no host to classify and is
    /// refused by the scope checks anyway.
    pub fn host(&self) -> Option<&str> {
        match self {
            Self::Tuple { host, .. } => Some(host),
            Self::Opaque { .. } => None,
        }
    }

    /// Whether two origins are the same origin.
    ///
    /// Same-origin means equal, and equal means every component matches. There
    /// is no same-site relaxation here: the protocol binds actions to origins,
    /// not to sites.
    pub fn is_same_origin(&self, other: &Self) -> bool {
        self == other
    }

    /// The serialization to show in trusted browser UI and to record in an
    /// audit event.
    ///
    /// An opaque origin serializes to a fixed local template plus nothing: its
    /// identifier is session-local noise to a reader and its precursor is not
    /// recoverable by design.
    pub fn display(&self) -> String {
        match self {
            Self::Tuple { scheme, host, port } => match (host.contains(':'), port) {
                (true, Some(port)) => format!("{scheme}://[{host}]:{port}"),
                (true, None) => format!("{scheme}://[{host}]"),
                (false, Some(port)) => format!("{scheme}://{host}:{port}"),
                (false, None) => format!("{scheme}://{host}"),
            },
            Self::Opaque { .. } => "opaque origin".to_owned(),
        }
    }

    /// The wire form, for a message that has to carry this origin onward.
    pub fn to_wire(&self) -> Origin {
        match self {
            Self::Tuple { .. } => Origin {
                kind: OriginKind::Tuple,
                serialization: Some(self.display()),
                opaque_id: None,
            },
            Self::Opaque { opaque_id } => Origin {
                kind: OriginKind::Opaque,
                serialization: None,
                opaque_id: Some(opaque_id.clone()),
            },
        }
    }
}

impl fmt::Display for NormalizedOrigin {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.display())
    }
}

/// Normalizes an origin taken from a protocol message.
///
/// Scheme and host are lowercased, a default port is dropped, and anything that
/// is not exactly `scheme://host[:port]` is refused. Refusal is the point: an
/// origin that cannot be normalized cannot be compared, and an origin that
/// cannot be compared cannot authorize anything.
pub fn normalize(origin: &Origin) -> Result<NormalizedOrigin, OriginError> {
    match origin.kind {
        OriginKind::Opaque => {
            if origin.serialization.is_some() {
                return Err(OriginError::MismatchedFields);
            }
            let opaque_id = origin
                .opaque_id
                .as_deref()
                .filter(|value| !value.is_empty())
                .ok_or(OriginError::MissingOpaqueIdentifier)?;
            Ok(NormalizedOrigin::Opaque {
                opaque_id: opaque_id.to_owned(),
            })
        }
        OriginKind::Tuple => {
            if origin.opaque_id.is_some() {
                return Err(OriginError::MismatchedFields);
            }
            let serialization = origin
                .serialization
                .as_deref()
                .ok_or(OriginError::MissingSerialization)?;
            normalize_serialization(serialization)
        }
    }
}

/// Normalizes a `scheme://host[:port]` serialization.
///
/// Exposed because the browser broker and the approval surface both hold an
/// origin as text before it becomes a protocol value.
pub fn normalize_serialization(serialization: &str) -> Result<NormalizedOrigin, OriginError> {
    let (scheme, rest) = serialization
        .split_once("://")
        .ok_or(OriginError::MalformedSerialization)?;

    if !valid_scheme(scheme) {
        return Err(OriginError::MalformedSerialization);
    }
    if rest
        .bytes()
        .any(|byte| matches!(byte, b'/' | b'?' | b'#' | b'@' | b'\\'))
    {
        return Err(OriginError::NotAnOrigin);
    }

    let scheme = scheme.to_ascii_lowercase();
    let (host, port_text) = split_host_and_port(rest)?;
    let host = normalize_host(host).ok_or(OriginError::MalformedSerialization)?;

    let port = match port_text {
        None => None,
        Some(text) => {
            let parsed: u16 = text.parse().map_err(|_| OriginError::MalformedPort)?;
            if default_port(&scheme) == Some(parsed) {
                None
            } else {
                Some(parsed)
            }
        }
    };

    Ok(NormalizedOrigin::Tuple { scheme, host, port })
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

/// Splits `host[:port]`, keeping a bracketed address literal intact.
fn split_host_and_port(rest: &str) -> Result<(&str, Option<&str>), OriginError> {
    if let Some(after_bracket) = rest.strip_prefix('[') {
        let (host, tail) = after_bracket
            .split_once(']')
            .ok_or(OriginError::MalformedSerialization)?;
        if tail.is_empty() {
            return Ok((host, None));
        }
        let port = tail
            .strip_prefix(':')
            .ok_or(OriginError::MalformedSerialization)?;
        return Ok((host, Some(port)));
    }

    match rest.split_once(':') {
        None => Ok((rest, None)),
        Some((host, port)) if !port.contains(':') => Ok((host, Some(port))),
        Some(_) => Err(OriginError::MalformedSerialization),
    }
}

/// The explicit set of origins a navigation may land on.
///
/// An empty set forbids redirects. Membership is exact equality of normalized
/// origins, so an opaque origin is admitted only by an entry that is the same
/// opaque origin, and a tuple entry never admits an opaque landing.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AllowedRedirects {
    origins: Vec<NormalizedOrigin>,
}

impl AllowedRedirects {
    /// The set that forbids every redirect.
    pub const fn none() -> Self {
        Self {
            origins: Vec::new(),
        }
    }

    /// Builds a set from normalized origins, discarding duplicates.
    pub fn from_normalized(origins: impl IntoIterator<Item = NormalizedOrigin>) -> Self {
        let mut collected: Vec<NormalizedOrigin> = origins.into_iter().collect();
        collected.sort();
        collected.dedup();
        Self { origins: collected }
    }

    /// Builds a set from protocol origins.
    ///
    /// One unnormalizable entry fails the whole set: a partially understood
    /// redirect policy is not a redirect policy.
    pub fn from_wire(origins: &[Origin]) -> Result<Self, OriginError> {
        let mut collected = Vec::with_capacity(origins.len());
        for origin in origins {
            collected.push(normalize(origin)?);
        }
        Ok(Self::from_normalized(collected))
    }

    /// Whether a navigation that landed on `origin` stayed inside the set.
    pub fn permits(&self, origin: &NormalizedOrigin) -> bool {
        self.origins.binary_search(origin).is_ok()
    }

    /// Whether the set forbids every redirect.
    pub fn forbids_all(&self) -> bool {
        self.origins.is_empty()
    }

    /// The origins in the set, sorted and deduplicated.
    pub fn origins(&self) -> &[NormalizedOrigin] {
        &self.origins
    }
}

/// Whether a frame may be observed or acted on.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FrameEligibility {
    /// The frame is inside the task's origin scope.
    Eligible,
    /// The frame is outside it, with the protocol's reason for the omission.
    Excluded(FrameExclusionReason),
}

impl FrameEligibility {
    /// Whether the frame may be observed or acted on.
    pub const fn is_eligible(self) -> bool {
        matches!(self, Self::Eligible)
    }
}

/// Whether a frame at `frame_origin` inside a document at `top_level_origin` is
/// in scope.
///
/// A same-origin frame is in scope. A cross-origin frame is in scope only when
/// the task's explicit allowlist names its exact normalized origin — the
/// allowlist is the user's declared source scope, not an inference. Every other
/// frame is excluded under the protocol's cross-origin policy reason.
///
/// An opaque frame is cross-origin against every tuple document, so a sandboxed
/// frame is excluded unless the allowlist names that exact opaque origin, which
/// only the browser broker can arrange within one session.
pub fn frame_eligibility(
    top_level_origin: &NormalizedOrigin,
    frame_origin: &NormalizedOrigin,
    allowed_frame_origins: &AllowedRedirects,
) -> FrameEligibility {
    if top_level_origin.is_same_origin(frame_origin) || allowed_frame_origins.permits(frame_origin)
    {
        FrameEligibility::Eligible
    } else {
        FrameEligibility::Excluded(FrameExclusionReason::CrossOriginPolicy)
    }
}

#[cfg(test)]
mod tests {
    use super::{
        frame_eligibility, normalize, normalize_serialization, AllowedRedirects, FrameEligibility,
        NormalizedOrigin, OriginError,
    };
    use bip_types::identity::{Origin, OriginKind};
    use bip_types::snapshot::FrameExclusionReason;

    fn tuple(serialization: &str) -> Origin {
        Origin {
            kind: OriginKind::Tuple,
            serialization: Some(serialization.to_owned()),
            opaque_id: None,
        }
    }

    fn opaque(id: &str) -> Origin {
        Origin {
            kind: OriginKind::Opaque,
            serialization: None,
            opaque_id: Some(id.to_owned()),
        }
    }

    fn normalized(serialization: &str) -> NormalizedOrigin {
        match normalize_serialization(serialization) {
            Ok(origin) => origin,
            Err(error) => unreachable!("fixture origin must normalize: {error}"),
        }
    }

    #[test]
    fn case_and_default_port_do_not_change_an_origin() {
        assert_eq!(
            normalize(&tuple("HTTPS://Example.TEST:443")),
            Ok(normalized("https://example.test"))
        );
        assert_eq!(
            normalize(&tuple("http://example.test:80")),
            Ok(normalized("http://example.test"))
        );
        assert_ne!(
            normalize(&tuple("https://example.test:8443")),
            Ok(normalized("https://example.test"))
        );
    }

    #[test]
    fn an_address_literal_keeps_its_brackets_and_its_port() {
        let normalized = normalize_serialization("https://[2001:db8::1]:8443")
            .expect("the address literal normalizes");
        assert_eq!(normalized.display(), "https://[2001:db8::1]:8443");
        assert_eq!(
            normalized.to_wire().serialization.as_deref(),
            Some("https://[2001:db8::1]:8443")
        );
    }

    #[test]
    fn a_url_is_not_an_origin() {
        assert_eq!(
            normalize_serialization("https://example.test/path"),
            Err(OriginError::NotAnOrigin)
        );
        assert_eq!(
            normalize_serialization("https://user@example.test"),
            Err(OriginError::NotAnOrigin)
        );
        assert_eq!(
            normalize_serialization("example.test"),
            Err(OriginError::MalformedSerialization)
        );
        assert_eq!(
            normalize_serialization("https://example.test:notaport"),
            Err(OriginError::MalformedPort)
        );
        assert_eq!(
            normalize_serialization("1https://example.test"),
            Err(OriginError::MalformedSerialization)
        );
        assert_eq!(
            normalize_serialization("https://not a host.example"),
            Err(OriginError::MalformedSerialization)
        );
    }

    #[test]
    fn an_opaque_origin_is_never_broadened_to_a_tuple_origin() {
        let sandboxed = normalize(&opaque("op_1")).unwrap_or(NormalizedOrigin::Opaque {
            opaque_id: String::new(),
        });
        let precursor = normalized("https://example.test");
        assert!(sandboxed.is_opaque());
        assert!(!sandboxed.is_same_origin(&precursor));

        // An allowlist naming the precursor does not admit the sandboxed frame.
        let allowed = AllowedRedirects::from_normalized([precursor.clone()]);
        assert!(!allowed.permits(&sandboxed));
        assert_eq!(
            frame_eligibility(&precursor, &sandboxed, &allowed),
            FrameEligibility::Excluded(FrameExclusionReason::CrossOriginPolicy)
        );

        // Only the same opaque identifier admits it.
        let exact = AllowedRedirects::from_normalized([NormalizedOrigin::Opaque {
            opaque_id: "op_1".to_owned(),
        }]);
        assert!(exact.permits(&sandboxed));
        // A different opaque identifier is a different origin.
        let other = AllowedRedirects::from_normalized([NormalizedOrigin::Opaque {
            opaque_id: "op_2".to_owned(),
        }]);
        assert!(!other.permits(&sandboxed));
    }

    #[test]
    fn an_empty_redirect_set_forbids_every_redirect() {
        let empty = AllowedRedirects::none();
        assert!(empty.forbids_all());
        assert!(!empty.permits(&normalized("https://example.test")));
    }

    #[test]
    fn a_redirect_set_admits_only_the_origins_it_names() {
        let allowed = AllowedRedirects::from_normalized([
            normalized("https://example.test"),
            normalized("https://cdn.example.test"),
            normalized("https://example.test"),
        ]);
        assert_eq!(allowed.origins().len(), 2);
        assert!(allowed.permits(&normalized("https://example.test")));
        // A subdomain, a scheme downgrade, and a different port are all
        // different origins.
        assert!(!allowed.permits(&normalized("https://other.example.test")));
        assert!(!allowed.permits(&normalized("http://example.test")));
        assert!(!allowed.permits(&normalized("https://example.test:8443")));
    }

    #[test]
    fn a_cross_origin_frame_needs_an_explicit_allowlist_entry() {
        let top = normalized("https://example.test");
        let frame = normalized("https://widget.example.test");
        assert_eq!(
            frame_eligibility(&top, &frame, &AllowedRedirects::none()),
            FrameEligibility::Excluded(FrameExclusionReason::CrossOriginPolicy)
        );
        assert!(frame_eligibility(&top, &top, &AllowedRedirects::none()).is_eligible());
        let allowed = AllowedRedirects::from_normalized([frame.clone()]);
        assert!(frame_eligibility(&top, &frame, &allowed).is_eligible());
    }

    #[test]
    fn a_malformed_origin_message_is_refused_rather_than_repaired() {
        assert_eq!(
            normalize(&Origin {
                kind: OriginKind::Tuple,
                serialization: None,
                opaque_id: None
            }),
            Err(OriginError::MissingSerialization)
        );
        assert_eq!(
            normalize(&Origin {
                kind: OriginKind::Opaque,
                serialization: None,
                opaque_id: None
            }),
            Err(OriginError::MissingOpaqueIdentifier)
        );
        assert_eq!(
            normalize(&Origin {
                kind: OriginKind::Opaque,
                serialization: Some("https://example.test".to_owned()),
                opaque_id: Some("op_1".to_owned()),
            }),
            Err(OriginError::MismatchedFields)
        );
    }
}
