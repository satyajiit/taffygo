// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Opt-in local interface measurements; no browser, disk or provider latency.

mod scheduling;

use std::hint::black_box;
use std::time::Instant;

use taffy_storage::ids::MemoryId;
use taffy_storage::memory::{
    MemoryQuery, MemoryRecord, MemoryScope, MemorySearchAudience, MemorySensitivity, MemorySource,
    MemoryStore, MAX_MEMORY_RECORDS,
};
use task_engine::{EffectiveToolSet, Milestone};

/// Invoked only by `tools/benchmark --local-loop`, never as a timing gate.
#[test]
#[ignore = "opt-in measured host workload; run tools/benchmark --local-loop"]
fn measure_local_loop_interfaces() {
    assert!(
        !black_box(cfg!(debug_assertions)),
        "measure the release profile"
    );
    let runs = Runs {
        samples: count("TAFFY_LOCAL_LATENCY_SAMPLES", 101),
        iterations: count("TAFFY_LOCAL_LATENCY_ITERATIONS", 50),
    };
    runs.measure("timer_control", || (), |()| black_box(0_u64), |(), _| {});
    tools(&runs);
    memory(&runs);
    scheduling::run(&runs);
}

struct Runs {
    samples: usize,
    iterations: usize,
}

impl Runs {
    /// Setup, validation and returned-value destruction are outside each
    /// timed interface call. Raw batch totals retain every measured sample.
    fn measure<C, T>(
        &self,
        name: &str,
        mut setup: impl FnMut() -> C,
        mut call: impl FnMut(&mut C) -> T,
        check: impl Fn(&C, &T),
    ) {
        let mut validation = setup();
        let result = call(&mut validation);
        check(&validation, &result);
        let totals: Vec<_> = (0..self.samples)
            .map(|_| {
                (0..self.iterations)
                    .map(|_| {
                        let mut context = setup();
                        let start = Instant::now();
                        let result = black_box(call(black_box(&mut context)));
                        let elapsed = start.elapsed().as_nanos();
                        check(&context, &result);
                        elapsed
                    })
                    .sum::<u128>()
            })
            .collect();
        println!(
            "\nTAFFY_LOCAL_LATENCY_V1={{\"case\":\"{name}\",\"samples\":{},\"iterations\":{},\"batch_totals_ns\":{totals:?}}}",
            self.samples, self.iterations
        );
    }
}

fn count(name: &str, default: usize) -> usize {
    std::env::var(name).map_or(default, |value| {
        let count = value.parse::<usize>().unwrap();
        assert!((1..=10_000).contains(&count), "bounded sample count");
        count
    })
}

fn tools(runs: &Runs) {
    let admitted = EffectiveToolSet::for_task(Milestone::M8, &[]);
    let query = "download";
    let expected = admitted.search_names(query);
    assert!(expected.contains(&"browser.download.from_link"));
    println!(
        "\nTAFFY_LOCAL_WORKLOAD_V1={{\"name\":\"tools\",\"admitted_rows\":{},\"deferred_rows\":{},\"query\":\"{query}\",\"result_count\":{}}}",
        admitted.entries().len(), admitted.deferred().len(), expected.len()
    );
    runs.measure(
        "tools_construct_and_discover",
        || (),
        |()| EffectiveToolSet::for_task(Milestone::M8, &[]).search_names(black_box(query)),
        |(), names| assert_eq!(names, &expected),
    );
    runs.measure(
        "tools_discover_resident",
        || (),
        |()| admitted.search_names(black_box(query)),
        |(), names| assert_eq!(names, &expected),
    );
}

fn memory(runs: &Runs) {
    let records: Vec<_> = (0..MAX_MEMORY_RECORDS).map(memory_record).collect();
    let query = MemoryQuery::new("repairable phone", 8).unwrap();
    let audience = MemorySearchAudience::Task { workspace_id: None };
    println!(
        "\nTAFFY_LOCAL_WORKLOAD_V1={{\"name\":\"memory\",\"records\":{},\"statement_bytes_min\":{},\"statement_bytes_max\":{},\"query\":\"repairable phone\",\"limit\":8,\"audience\":\"regular_task\"}}",
        records.len(), records.iter().map(|r| r.statement.len()).min().unwrap(),
        records.iter().map(|r| r.statement.len()).max().unwrap()
    );
    let restore = || {
        let mut store = MemoryStore::new();
        store.restore(512, records.clone()).unwrap();
        store
    };
    let check = |hits: &Vec<taffy_storage::memory::MemoryHit>| {
        assert_eq!(hits.len(), 8);
        assert!(hits
            .iter()
            .all(|hit| hit.record.statement.contains("repairable phone")));
    };
    runs.measure(
        "memory_candidates_cold_512",
        restore,
        |store| store.search(black_box(&query), audience, 1_000),
        |_, hits| check(hits),
    );
    let primed = restore();
    check(&primed.search(&query, audience, 1_000));
    runs.measure(
        "memory_candidates_warm_512",
        || (),
        |()| primed.search(black_box(&query), audience, black_box(1_000)),
        |(), hits| check(hits),
    );
}

fn memory_record(index: usize) -> MemoryRecord {
    MemoryRecord {
        memory_id: MemoryId::from_bytes(u128::try_from(index + 1).unwrap().to_be_bytes()),
        revision: 1,
        statement: format!(
            "{} {}End.",
            if index.is_multiple_of(4) {
                "Choose a repairable phone"
            } else {
                "Prefer a durable laptop"
            },
            "A public synthetic preference for comparing price, warranty and availability. "
                .repeat(3)
        ),
        source: MemorySource::UserEntered,
        scope: MemoryScope::AllTasks,
        sensitivity: MemorySensitivity::Standard,
        created_at_epoch_ms: 100,
        updated_at_epoch_ms: u64::try_from(index + 100).unwrap(),
        reviewed_at_epoch_ms: None,
        expires_at_epoch_ms: None,
    }
}
