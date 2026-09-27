// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The account and workspace subsystems are ports, not concrete state.
//!
//! These cases build a runtime whose account and workspace planes are test
//! doubles and then drive one command through it, so the seam is proven by
//! substitution rather than by the shape of the type.

use std::cell::RefCell;
use std::rc::Rc;

use core_api_types::{AuthPhase, CoreAvailability};

use crate::contract::ServiceGeneration;
use crate::ports::WorkspaceStoreError;
use crate::runtime::{CoreRuntime, ServiceRuntimeComponents};

use crate::adapters::assets::{GenerationJitter, ProductionAssetDelivery};

use super::port_doubles::{FakeAccount, FakeWorkspaces, SUBSTITUTED_WORKSPACE};
use super::{
    begin_for, empty_runtime, must_open, storage_success, task_id, Encoder, FakeFactory,
    FakeModels, FakePolicy,
};

struct Substituted {
    runtime: CoreRuntime,
    account_calls: Rc<RefCell<Vec<&'static str>>>,
    workspace_calls: Rc<RefCell<Vec<&'static str>>>,
}

fn substituted_runtime(
    port_calls: &Rc<RefCell<Vec<&'static str>>>,
    pending_flows: usize,
) -> Substituted {
    let account_calls = Rc::new(RefCell::new(Vec::new()));
    let workspace_calls = Rc::new(RefCell::new(Vec::new()));
    let encoder = Encoder {
        calls: Rc::clone(port_calls),
        fail_storage: false,
    };
    let runtime = CoreRuntime::new(
        ServiceGeneration::INITIAL,
        ServiceRuntimeComponents {
            digest: Rc::new(crate::account::crypto::ReferenceSha256),
            task_factory: Box::new(FakeFactory { revision: 0 }),
            policy: Box::new(FakePolicy),
            audit: Box::new(encoder.clone()),
            models: Box::new(FakeModels),
            storage: Box::new(encoder),
            account: Box::new(FakeAccount {
                calls: Rc::clone(&account_calls),
                session: None,
                pending: pending_flows,
                ..FakeAccount::default()
            }),
            workspaces: Box::new(FakeWorkspaces {
                calls: Rc::clone(&workspace_calls),
            }),
            library: Box::new(crate::ProductionLibrary::new(false)),
            memory: Box::new(crate::ProductionMemory::new(false)),
            assets: Box::new(ProductionAssetDelivery::new(Box::new(
                GenerationJitter::new([0u8; 32]),
            ))),
            observers: Vec::new(),
        },
    );
    Substituted {
        runtime,
        account_calls,
        workspace_calls,
    }
}

#[test]
fn a_command_runs_through_substituted_account_and_workspace_ports() {
    let port_calls = Rc::new(RefCell::new(Vec::new()));
    let Substituted {
        mut runtime,
        account_calls,
        workspace_calls,
    } = substituted_runtime(&port_calls, 1);
    must_open(&mut runtime, "task-1");
    port_calls.borrow_mut().clear();

    let commit = begin_for(&mut runtime, &task_id(), "substituted-submit");
    assert!(matches!(
        runtime.complete_commit(&task_id(), storage_success(&commit), 2),
        Ok(crate::runtime::CommitOutcome::Committed(_))
    ));
    assert_eq!(
        port_calls.borrow().as_slice(),
        ["audit_encode", "storage_encode"]
    );

    let status = runtime
        .project_core_status(CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    let workspace = status
        .workspaces
        .first()
        .unwrap_or_else(|| unreachable!())
        .workspace_id
        .clone();
    assert_eq!(workspace, SUBSTITUTED_WORKSPACE);
    assert_eq!(
        status.auth_state.unwrap_or_else(|| unreachable!()).phase,
        AuthPhase::InFlight
    );
    assert!(workspace_calls.borrow().contains(&"project_core_api"));
    assert!(account_calls.borrow().contains(&"pending_count"));
}

#[test]
fn the_substituted_account_port_receives_bootstrap_restore() {
    let port_calls = Rc::new(RefCell::new(Vec::new()));
    let Substituted {
        mut runtime,
        account_calls,
        ..
    } = substituted_runtime(&port_calls, 0);
    account_calls.borrow_mut().clear();
    assert!(runtime.restore_account_session(None).is_ok());
    assert!(account_calls.borrow().contains(&"restore_session"));
}

#[test]
fn a_substituted_account_with_a_flow_in_flight_refuses_bootstrap_restore() {
    let port_calls = Rc::new(RefCell::new(Vec::new()));
    let Substituted { mut runtime, .. } = substituted_runtime(&port_calls, 1);
    assert_eq!(
        runtime.restore_account_session(None),
        Err(crate::runtime::AccountRestoreError::AccountAlreadyActive)
    );
}

#[test]
fn the_production_workspace_plane_is_not_reachable_after_substitution() {
    let port_calls = Rc::new(RefCell::new(Vec::new()));
    let Substituted {
        mut runtime,
        workspace_calls,
        ..
    } = substituted_runtime(&port_calls, 1);
    workspace_calls.borrow_mut().clear();
    assert_eq!(
        runtime.begin_workspace_exclusion(
            "operation-1".to_owned(),
            SUBSTITUTED_WORKSPACE,
            9,
            "source-1",
            1,
        ),
        Err(WorkspaceStoreError::UnknownWorkspace)
    );
    assert_eq!(workspace_calls.borrow().as_slice(), ["begin_exclusion"]);

    let mut canonical = empty_runtime(false, &port_calls);
    assert_eq!(
        canonical.begin_workspace_exclusion(
            "operation-1".to_owned(),
            SUBSTITUTED_WORKSPACE,
            9,
            "source-1",
            1,
        ),
        Err(WorkspaceStoreError::InvalidIdentifier)
    );
}
