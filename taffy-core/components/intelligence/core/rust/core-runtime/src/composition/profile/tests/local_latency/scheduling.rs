// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reuses the task reducer fixtures, with real SHA-256 for proposal material.

#[path = "../../../../../../task-engine/tests/common/mod.rs"]
mod common;

use crate::account::crypto::ReferenceSha256;
use crate::account::Sha256Port;
use task_engine::{
    Command, ConsentedSource, PreModelObservation, ProviderRouteId, TaskTemplateId, WorkflowDigest,
    WorkflowError,
};

use super::Runs;

struct Digest;

impl WorkflowDigest for Digest {
    fn sha256(&self, bytes: &[u8]) -> Result<[u8; 32], WorkflowError> {
        ReferenceSha256
            .sha256(bytes)
            .map_err(|_| WorkflowError::DigestUnavailable)
    }
}

pub(super) fn run(runs: &Runs) {
    println!(
        "\nTAFFY_LOCAL_WORKLOAD_V1={{\"name\":\"scheduling\",\"parallel_cap\":{},\"digest\":\"reference_sha256\",\"durability\":\"in_memory_reducer_journal\"}}",
        task_engine::MAX_PARALLEL_SOURCE_READS
    );
    for count in [1, 4] {
        let name = format!("schedule_{count}_source_reads");
        runs.measure(
            &name,
            || running(count),
            |fixture| {
                for _ in 0..count {
                    let PreModelObservation::Command(command @ Command::ProposeAction(_)) = fixture
                        .reducer
                        .next_pre_model_observation(&[], &Digest)
                        .unwrap()
                    else {
                        panic!("a consented unread source must propose a read")
                    };
                    fixture.must_apply(command);
                }
            },
            |fixture, ()| {
                assert_eq!(fixture.reducer.actions().count(), usize::from(count));
                assert_eq!(
                    fixture.reducer.next_pre_model_observation(&[], &Digest),
                    Ok(PreModelObservation::Waiting)
                );
            },
        );
    }
    let pending = {
        let mut fixture = running(5);
        for _ in 0..task_engine::MAX_PARALLEL_SOURCE_READS {
            let PreModelObservation::Command(command) = fixture
                .reducer
                .next_pre_model_observation(&[], &Digest)
                .unwrap()
            else {
                panic!("a free source-read slot")
            };
            fixture.must_apply(command);
        }
        fixture
    };
    runs.measure(
        "schedule_fifth_source_waits",
        || (),
        |()| {
            pending
                .reducer
                .next_pre_model_observation(&[], &Digest)
                .unwrap()
        },
        |(), decision| assert_eq!(decision, &PreModelObservation::Waiting),
    );
}

fn running(count: u8) -> common::Fixture {
    let mut seed = common::seed();
    seed.snapshot.template_id = if count == 1 {
        TaskTemplateId::BuildSourceTable
    } else {
        TaskTemplateId::CompareProducts
    };
    seed.snapshot.provider_route = ProviderRouteId::new("direct_user_key").ok();
    seed.snapshot.tool_allowlist = vec![task_engine::REVIEWED_OBSERVATION_TOOL.to_owned()];
    let mut fixture = common::draft_from(seed);
    let mut preview = common::preview();
    for source in 2..=count {
        let source_id = common::source_id(source);
        preview.scope = preview.scope.include(source_id);
        preview.sources.push(ConsentedSource {
            source_id,
            tab_id: task_engine::deps::TabId::new(format!("tab_{source}")),
            normalized_origin: format!("https://source-{source}.example"),
            canonical_locator: None,
        });
    }
    preview.budgets = task_engine::TaskBudgets::none()
        .with(task_engine::BudgetKind::MaxSources, u64::from(count));
    preview.provider_route = ProviderRouteId::new("direct_user_key").ok();
    fixture.must_apply(Command::StartTask(preview));
    fixture.must_apply(Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(Command::ExecutorStarted);
    fixture
}
