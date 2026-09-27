// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A grant retains exactly the node and destination shape its operation needs.

use super::request;
use crate::action_class::{ActionClass, PolicyMilestone};
use crate::action_operation::ActionOperationKind;
use crate::capability::PolicyVersion;
use crate::denial::DenialReason;
use crate::grant::{GrantDecision, GrantPolicy};
use crate::risk::RiskClass;
use crate::time::SequentialIds;
use bip_types::identity::{MonotonicMillis, SemanticNodeId};

#[test]
fn grant_binds_principal_class_and_effect_identity_exactly() {
    let request = request();
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    let GrantDecision::Authorize(grant) = policy.decide(&request, MonotonicMillis(100)) else {
        unreachable!("valid observation must mint a grant")
    };
    assert_eq!(grant.principal, request.principal);
    assert_eq!(grant.action_class, request.action_class);
    assert_eq!(grant.operation_kind, request.operation_kind);
    assert_eq!(
        grant.canonical_intent_digest,
        request.canonical_intent_digest
    );
    assert_eq!(grant.idempotency_key, request.idempotency_key);
    assert_eq!(grant.proposal_digest, request.proposal_digest);
    assert_eq!(grant.scope, request.scope);
}

#[test]
fn a_navigation_grant_retains_the_exact_destination_address() {
    let mut request = request();
    request.action_class = ActionClass::OpenLink;
    request.operation_kind = ActionOperationKind::Navigate;
    request.scope.destination_scope = Some(request.scope.origin.clone());
    request.scope.destination_address =
        Some("https://example.test/orders?token=a%2Fb#receipt".to_owned());
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    let GrantDecision::Authorize(grant) = policy.decide(&request, MonotonicMillis(100)) else {
        unreachable!("a complete navigation must mint a grant")
    };
    assert_eq!(
        grant.scope.destination_address,
        request.scope.destination_address
    );
}

#[test]
fn tab_control_grants_are_exact_and_destination_free() {
    for operation_kind in [
        ActionOperationKind::HistoryBack,
        ActionOperationKind::HistoryForward,
        ActionOperationKind::Reload,
        ActionOperationKind::StopLoading,
    ] {
        let mut complete = request();
        complete.action_class = ActionClass::ControlTab;
        complete.operation_kind = operation_kind;
        let GrantDecision::Authorize(grant) =
            GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
                .decide(&complete, MonotonicMillis(100))
        else {
            unreachable!("a destination-free tab control must mint")
        };
        assert_eq!(grant.operation_kind, operation_kind);
        assert_eq!(grant.action_class, ActionClass::ControlTab);
        assert!(grant.scope.destination_scope.is_none());
        assert!(grant.scope.destination_address.is_none());

        let mut invented_destination = complete.clone();
        invented_destination.scope.destination_scope =
            Some(invented_destination.scope.origin.clone());
        invented_destination.scope.destination_address =
            Some("https://example.test/invented".to_owned());
        assert!(matches!(
            GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
                .decide(&invented_destination, MonotonicMillis(100)),
            GrantDecision::Deny(_)
        ));
    }
}

#[test]
fn link_open_requires_and_retains_one_node_and_exact_browser_destination() {
    let mut request = request();
    request.action_class = ActionClass::OpenLink;
    request.operation_kind = ActionOperationKind::LinkOpen;
    request.scope.destination_scope = Some(request.scope.origin.clone());
    request.scope.destination_address =
        Some("https://other.test/exact/path?x=1#fragment".to_owned());

    let mut missing_node = request.clone();
    missing_node.scope.node_id = None;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&missing_node, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));

    request.scope.node_id = Some(SemanticNodeId::new("link-7"));
    let GrantDecision::Authorize(grant) =
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&request, MonotonicMillis(100))
    else {
        unreachable!("a browser-resolved exact link target must mint")
    };
    assert_eq!(grant.scope.node_id, request.scope.node_id);
    assert_eq!(
        grant.scope.destination_scope,
        request.scope.destination_scope
    );
    assert_eq!(
        grant.scope.destination_address,
        request.scope.destination_address
    );
}

#[test]
fn exact_read_operations_cannot_substitute_each_others_node_shape() {
    let mut form = request();
    form.operation_kind = ActionOperationKind::FormInspect;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&form, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
    form.scope.node_id = Some(SemanticNodeId::new("form-1"));
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&form, MonotonicMillis(100)),
        GrantDecision::Authorize(_)
    ));

    let mut selection = request();
    selection.operation_kind = ActionOperationKind::SelectionRead;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&selection, MonotonicMillis(100)),
        GrantDecision::Authorize(_)
    ));
    selection.scope.node_id = Some(SemanticNodeId::new("invented-selection"));
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&selection, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}

#[test]
fn destination_address_presence_must_match_the_exact_operation() {
    let mut missing = request();
    missing.operation_kind = ActionOperationKind::Navigate;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&missing, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));

    let mut invented = request();
    invented.scope.destination_address = Some("https://example.test/elsewhere".to_owned());
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&invented, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));

    let mut mismatched = request();
    mismatched.operation_kind = ActionOperationKind::FormSubmit;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12))
            .decide(&mismatched, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}

#[test]
fn search_needs_one_exact_destination_and_no_node() {
    let mut request = request();
    request.action_class = ActionClass::OpenLink;
    request.operation_kind = ActionOperationKind::Search;
    request.context_risk = RiskClass::ReversibleDisclosure;
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    match policy.decide(&request, MonotonicMillis(100)) {
        GrantDecision::Deny(denial) => {
            assert_eq!(denial.reason, DenialReason::AuthorityContextInvalid);
        }
        other => unreachable!("expected an incomplete-operation denial, got {other:?}"),
    }
    request.scope.destination_scope = Some(request.scope.origin.clone());
    request.scope.destination_address = Some("https://example.test/search?q=bound".to_owned());
    assert!(matches!(
        policy.decide(&request, MonotonicMillis(100)),
        GrantDecision::Authorize(_)
    ));
}

/// A tab whose document has no site of its own still has somewhere to go.
///
/// The browser binds an opaque scope origin for Chromium's own error document
/// the way it does for a discovery blank, because neither has a site policy
/// could classify. Where the move lands is a tuple and that is what is
/// decided. Refusing this shape is what left an errand on an error page with
/// no admissible move (decision 0176).
#[test]
fn a_move_that_leaves_a_document_with_no_site_is_decided_by_where_it_lands() {
    let mut request = request();
    request.action_class = ActionClass::OpenLink;
    request.operation_kind = ActionOperationKind::Navigate;
    request.context_risk = RiskClass::ReversibleDisclosure;
    request.scope.origin = crate::origin::NormalizedOrigin::Opaque {
        opaque_id: "epoch-of-the-error-document".to_owned(),
    };
    request.scope.destination_scope = Some(
        crate::origin::normalize_serialization("https://myaadhaar.example")
            .unwrap_or_else(|_| unreachable!()),
    );
    request.scope.destination_address = Some("https://myaadhaar.example/".to_owned());
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    let GrantDecision::Authorize(grant) = policy.decide(&request, MonotonicMillis(100)) else {
        unreachable!("a move that leaves a site-less document must mint a grant")
    };
    assert_eq!(
        grant.scope.destination_address.as_deref(),
        Some("https://myaadhaar.example/")
    );

    // And the class bound still binds: standing on a document with no site is
    // not a way around the one class a typed address never reaches.
    let mut classified = request;
    classified.scope.destination_scope = Some(
        crate::origin::normalize_serialization("https://mail.google.com")
            .unwrap_or_else(|_| unreachable!()),
    );
    classified.scope.destination_address = Some("https://mail.google.com/mail/".to_owned());
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&classified, MonotonicMillis(100)),
        GrantDecision::Deny(denial) if denial.reason == DenialReason::DestinationClassRestricted
    ));
}
