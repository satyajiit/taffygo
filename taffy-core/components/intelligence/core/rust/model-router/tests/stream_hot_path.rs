// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Structural allocation regression for stream framing.

#![allow(unsafe_code)]
#![allow(clippy::expect_used, clippy::undocumented_unsafe_blocks)]

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::wire::fold_stream;
use model_router::wire::reply::ReplyContext;

struct CountingAllocator;

static TRACKING: AtomicBool = AtomicBool::new(false);
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if TRACKING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if TRACKING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { System.realloc(ptr, layout, new_size) }
    }
}

fn context() -> ReplyContext {
    ReplyContext {
        request_id: RequestId::from_bytes([4; 16]),
        context_window: 200_000,
        requested_answer_tokens: 2_000,
        reports_finish_reason: true,
    }
}

fn allocations_while_folding(body: &str) -> usize {
    ALLOCATIONS.store(0, Ordering::Relaxed);
    TRACKING.store(true, Ordering::Relaxed);
    let result = fold_stream(WireApi::AnthropicMessages, body, &context());
    TRACKING.store(false, Ordering::Relaxed);
    result.expect("the terminal prefix folds");
    ALLOCATIONS.load(Ordering::Relaxed)
}

#[test]
fn a_terminal_prefix_does_not_allocate_for_unread_frames() {
    let terminal = concat!(
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},",
        "\"usage\":{\"input_tokens\":1,\"output_tokens\":1}}\n\n",
    );
    let mut with_unread_suffix = String::from(terminal);
    for _ in 0..16_384 {
        with_unread_suffix.push_str("data: {\"type\":\"message_start\"}\n\n");
    }

    let prefix_allocations = allocations_while_folding(terminal);
    let suffix_allocations = allocations_while_folding(&with_unread_suffix);
    assert!(
        suffix_allocations <= prefix_allocations + 1,
        "unread suffix added {suffix_allocations} allocations versus {prefix_allocations} for the prefix"
    );
}
