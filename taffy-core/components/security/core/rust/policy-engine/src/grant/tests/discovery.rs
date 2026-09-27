// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The discovery fact mints exactly one bounded move — a search, or a typed
//! navigate to an https address — and widens nothing else.

use super::discovery_request;
use crate::action_class::PolicyMilestone;
use crate::action_operation::ActionOperationKind;
use crate::capability::PolicyVersion;
use crate::denial::DenialReason;
use crate::grant::{GrantDecision, GrantPolicy, GrantRequest, PolicyEvaluationContext};
use crate::origin::{normalize_serialization, NormalizedOrigin};
use crate::time::SequentialIds;
use bip_types::action::PrincipalKind;
use bip_types::identity::{MonotonicMillis, TabId};

#[test]
fn discovery_mints_only_the_exact_opaque_tab_search_fact() {
    let request = discovery_request();
    let GrantDecision::Authorize(grant) =
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&request, MonotonicMillis(100))
    else {
        unreachable!("the exact bounded discovery search must mint")
    };
    assert_eq!(grant.discovery, request.discovery);
    assert_eq!(grant.scope, request.scope);
    assert_eq!(grant.operation_kind, ActionOperationKind::Search);
}

#[test]
fn discovery_fact_never_widens_an_ordinary_task_context() {
    let mut request = discovery_request();
    request.context = PolicyEvaluationContext::Task;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&request, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}

#[test]
fn malformed_or_mismatched_discovery_authority_fails_closed() {
    let assert_denied = |request: &GrantRequest| {
        assert!(matches!(
            GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
                .decide(request, MonotonicMillis(100)),
            GrantDecision::Deny(_)
        ));
    };

    let mut missing = discovery_request();
    missing.discovery = None;
    assert_denied(&missing);

    let mut wrong_tab = discovery_request();
    wrong_tab
        .discovery
        .as_mut()
        .unwrap_or_else(|| unreachable!())
        .discovery_tab_id = TabId::new("different-tab");
    assert_denied(&wrong_tab);

    let mut wrong_session = discovery_request();
    wrong_session
        .discovery
        .as_mut()
        .unwrap_or_else(|| unreachable!())
        .browser_session_id
        .clear();
    assert_denied(&wrong_session);

    for cap in [0, 9] {
        let mut wrong_cap = discovery_request();
        wrong_cap
            .discovery
            .as_mut()
            .unwrap_or_else(|| unreachable!())
            .remaining_new_source_cap = cap;
        assert_denied(&wrong_cap);
    }

    let mut tuple_current_origin = discovery_request();
    tuple_current_origin.scope.origin =
        normalize_serialization("https://search.example").unwrap_or_else(|_| unreachable!());
    assert_denied(&tuple_current_origin);

    let mut opaque_destination = discovery_request();
    opaque_destination.scope.destination_scope = Some(NormalizedOrigin::Opaque {
        opaque_id: "invented-destination".to_owned(),
    });
    assert_denied(&opaque_destination);

    let mut wrong_operation = discovery_request();
    wrong_operation.operation_kind = ActionOperationKind::TabsOpen;
    assert_denied(&wrong_operation);

    let mut skill_principal = discovery_request();
    skill_principal.principal.kind = PrincipalKind::Skill;
    skill_principal.principal.skill_version_id = None;
    assert_denied(&skill_principal);
}

/// A typed address is a lead (decision 0106 section 2): the discovery fact
/// admits a navigate to it from the blank tab exactly as it admits the
/// search, and the grant carries the address's own origin as the destination.
#[test]
fn discovery_mints_a_typed_navigate_under_the_same_bounded_fact() {
    let mut request = discovery_request();
    request.operation_kind = ActionOperationKind::Navigate;
    request.scope.destination_scope = Some(
        normalize_serialization("https://myaadhaar.uidai.gov.in")
            .unwrap_or_else(|_| unreachable!()),
    );
    request.scope.destination_address = Some("https://myaadhaar.uidai.gov.in/".to_owned());
    let GrantDecision::Authorize(grant) =
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&request, MonotonicMillis(100))
    else {
        unreachable!("the exact bounded discovery navigate must mint")
    };
    assert_eq!(grant.discovery, request.discovery);
    assert_eq!(grant.operation_kind, ActionOperationKind::Navigate);
    assert_eq!(
        grant.scope.destination_scope,
        request.scope.destination_scope
    );
}

/// The one class of site a typed address is never admitted to on its own
/// (decision 0087 section 2 as amended). The refusal is a decision about the
/// proposal, under the code the task engine records and the model is told; the
/// same address is not refused when the task already stands on that origin,
/// because then the person's scope named it.
#[test]
fn a_typed_navigate_to_a_classified_destination_is_refused_by_class() {
    let mailbox =
        normalize_serialization("https://mail.google.com").unwrap_or_else(|_| unreachable!());
    let mut discovery = discovery_request();
    discovery.operation_kind = ActionOperationKind::Navigate;
    discovery.scope.destination_scope = Some(mailbox.clone());
    discovery.scope.destination_address = Some("https://mail.google.com/mail/".to_owned());
    let GrantDecision::Deny(denial) =
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&discovery, MonotonicMillis(100))
    else {
        unreachable!("a classified destination must be refused")
    };
    assert_eq!(denial.reason, DenialReason::DestinationClassRestricted);

    let mut from_a_page = super::request();
    from_a_page.action_class = crate::action_class::ActionClass::OpenLink;
    from_a_page.operation_kind = ActionOperationKind::Navigate;
    from_a_page.context_risk = crate::risk::RiskClass::ReversibleDisclosure;
    from_a_page.scope.destination_scope = Some(mailbox.clone());
    from_a_page.scope.destination_address = Some("https://mail.google.com/mail/".to_owned());
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&from_a_page, MonotonicMillis(100)),
        GrantDecision::Deny(denial) if denial.reason == DenialReason::DestinationClassRestricted
    ));

    let mut already_there = from_a_page;
    already_there.scope.origin = mailbox;
    assert!(matches!(
        GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(12))
            .decide(&already_there, MonotonicMillis(100)),
        GrantDecision::Authorize(_)
    ));
}
