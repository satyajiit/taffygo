// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! C ABI over the vendored adblock-rust engine. Not a TaffyGo workspace crate.

#![allow(clippy::missing_safety_doc)]

use std::collections::HashSet;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;
use std::slice;

use adblock::engine::Engine;
use adblock::lists::{FilterSet, ParseOptions, ParsedLine, parse_filter};
use adblock::request::Request;

pub struct TaffyAdblockEngine {
    inner: Engine,
}

#[repr(C)]
pub struct TaffyAdblockCompileStats {
    pub rules_indexed: u64,
    pub cosmetic_rules: u64,
    pub parse_errors: u64,
}

#[repr(C)]
pub struct TaffyAdblockMatchResult {
    pub matched: bool,
    pub important: bool,
    pub has_exception: bool,
}

#[repr(C)]
pub struct TaffyAdblockStringView {
    pub data: *const c_char,
    pub len: usize,
}

unsafe fn cstr<'a>(ptr: *const c_char) -> Option<&'a str> {
    if ptr.is_null() {
        return Some("");
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().ok()
}

unsafe fn view_str<'a>(view: TaffyAdblockStringView) -> Option<&'a str> {
    if view.len == 0 {
        return Some("");
    }
    if view.data.is_null() {
        return None;
    }
    let bytes = unsafe { slice::from_raw_parts(view.data.cast::<u8>(), view.len) };
    std::str::from_utf8(bytes).ok()
}

unsafe fn collect_strings(ptrs: *const *const c_char, len: usize) -> Option<Vec<String>> {
    if len == 0 {
        return Some(Vec::new());
    }
    if ptrs.is_null() {
        return None;
    }
    let slice = unsafe { slice::from_raw_parts(ptrs, len) };
    let mut out = Vec::with_capacity(len);
    for item in slice {
        out.push(unsafe { cstr(*item) }?.to_owned());
    }
    Some(out)
}

fn malloc_cstring(text: &str) -> *mut c_char {
    CString::new(text)
        .ok()
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

fn is_inert_line(line: &str) -> bool {
    let trimmed = line.trim();
    trimmed.is_empty()
        || trimmed.starts_with('!')
        || trimmed.starts_with("[Adblock")
        || (trimmed.starts_with('#')
            && trimmed.len() > 1
            && trimmed.as_bytes()[1].is_ascii_whitespace())
}

fn compile_stats(text: &str) -> TaffyAdblockCompileStats {
    let mut network = 0_u64;
    let mut cosmetic = 0_u64;
    let mut errors = 0_u64;
    let options = ParseOptions::default();
    for line in text.lines() {
        match parse_filter(line, false, options) {
            Ok(ParsedLine::Network(_)) => network += 1,
            Ok(ParsedLine::Cosmetic(_)) => cosmetic += 1,
            Err(_) if is_inert_line(line) => {}
            Err(_) => errors += 1,
        }
    }
    TaffyAdblockCompileStats {
        rules_indexed: network + cosmetic,
        cosmetic_rules: cosmetic,
        parse_errors: errors,
    }
}

fn engine_from_list(text: String) -> Engine {
    let mut filter_set = FilterSet::new(false);
    filter_set.add_filter_list(text, ParseOptions::default());
    Engine::new_with_filter_set(filter_set)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_create_from_list(
    bytes: *const u8,
    len: usize,
    stats: *mut TaffyAdblockCompileStats,
) -> *mut TaffyAdblockEngine {
    if bytes.is_null() && len != 0 {
        return ptr::null_mut();
    }
    let slice = if len == 0 {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(bytes, len) }
    };
    let Ok(text) = std::str::from_utf8(slice) else {
        return ptr::null_mut();
    };
    if !stats.is_null() {
        unsafe {
            *stats = compile_stats(text);
        }
    }
    let engine = engine_from_list(text.to_owned());
    Box::into_raw(Box::new(TaffyAdblockEngine { inner: engine }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_create_from_serialized(
    bytes: *const u8,
    len: usize,
) -> *mut TaffyAdblockEngine {
    if bytes.is_null() || len == 0 {
        return ptr::null_mut();
    }
    let slice = unsafe { slice::from_raw_parts(bytes, len) };
    let mut engine = Engine::default();
    if engine.deserialize(slice).is_err() {
        return ptr::null_mut();
    }
    Box::into_raw(Box::new(TaffyAdblockEngine { inner: engine }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_destroy(engine: *mut TaffyAdblockEngine) {
    if !engine.is_null() {
        drop(unsafe { Box::from_raw(engine) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_serialize(
    engine: *const TaffyAdblockEngine,
    len: *mut usize,
) -> *mut u8 {
    if engine.is_null() || len.is_null() {
        return ptr::null_mut();
    }
    let bytes = unsafe { &*engine }.inner.serialize();
    if bytes.is_empty() {
        unsafe {
            *len = 0;
        }
        return ptr::null_mut();
    }
    let mut boxed = bytes.into_boxed_slice();
    unsafe {
        *len = boxed.len();
    }
    let ptr = boxed.as_mut_ptr();
    std::mem::forget(boxed);
    ptr
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_bytes_free(ptr: *mut u8, len: usize) {
    if ptr.is_null() || len == 0 {
        return;
    }
    drop(unsafe { Box::from_raw(slice::from_raw_parts_mut(ptr, len)) });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_matches(
    engine: *const TaffyAdblockEngine,
    url: TaffyAdblockStringView,
    hostname: TaffyAdblockStringView,
    initiator_hostname: TaffyAdblockStringView,
    request_type: TaffyAdblockStringView,
    third_party: bool,
    method: TaffyAdblockStringView,
    disable_generic_rules: bool,
) -> TaffyAdblockMatchResult {
    let empty = TaffyAdblockMatchResult {
        matched: false,
        important: false,
        has_exception: false,
    };
    if engine.is_null() {
        return empty;
    }
    let (Some(url), Some(hostname), Some(initiator), Some(request_type), Some(method)) = (
        unsafe { view_str(url) },
        unsafe { view_str(hostname) },
        unsafe { view_str(initiator_hostname) },
        unsafe { view_str(request_type) },
        unsafe { view_str(method) },
    ) else {
        return empty;
    };
    let result = unsafe { &*engine }.inner.check_network_request_subset(
        &Request::preparsed(url, hostname, initiator, request_type, third_party, method),
        false,
        true,
        disable_generic_rules,
    );
    TaffyAdblockMatchResult {
        matched: result.should_block(),
        important: result.important,
        has_exception: result.exception.is_some(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_url_cosmetic_resources(
    engine: *const TaffyAdblockEngine,
    url: TaffyAdblockStringView,
) -> *mut c_char {
    if engine.is_null() {
        return ptr::null_mut();
    }
    let Some(url) = (unsafe { view_str(url) }) else {
        return ptr::null_mut();
    };
    let resources = unsafe { &*engine }.inner.url_cosmetic_resources(url);
    match serde_json::to_string(&resources) {
        Ok(json) => malloc_cstring(&json),
        Err(_) => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_hidden_class_id_selectors(
    engine: *const TaffyAdblockEngine,
    classes: *const *const c_char,
    n_classes: usize,
    ids: *const *const c_char,
    n_ids: usize,
    exceptions: *const *const c_char,
    n_exceptions: usize,
) -> *mut c_char {
    if engine.is_null() {
        return ptr::null_mut();
    }
    let Some(classes) = (unsafe { collect_strings(classes, n_classes) }) else {
        return ptr::null_mut();
    };
    let Some(ids) = (unsafe { collect_strings(ids, n_ids) }) else {
        return ptr::null_mut();
    };
    let Some(exceptions) = (unsafe { collect_strings(exceptions, n_exceptions) }) else {
        return ptr::null_mut();
    };
    let exception_set: HashSet<String> = exceptions.into_iter().collect();
    let selectors =
        unsafe { &*engine }
            .inner
            .hidden_class_id_selectors(&classes, &ids, &exception_set);
    match serde_json::to_string(&selectors) {
        Ok(json) => malloc_cstring(&json),
        Err(_) => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_generic_hide(
    engine: *const TaffyAdblockEngine,
    url: TaffyAdblockStringView,
) -> bool {
    if engine.is_null() {
        return false;
    }
    let Some(url) = (unsafe { view_str(url) }) else {
        return false;
    };
    unsafe { &*engine }
        .inner
        .url_cosmetic_resources(url)
        .generichide
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_engine_generic_block(
    engine: *const TaffyAdblockEngine,
    url: TaffyAdblockStringView,
) -> bool {
    if engine.is_null() {
        return false;
    }
    let Some(url) = (unsafe { view_str(url) }) else {
        return false;
    };
    unsafe { &*engine }.inner.check_generic_block(url)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn taffy_adblock_string_free(ptr: *mut c_char) {
    if ptr.is_null() {
        return;
    }
    drop(unsafe { CString::from_raw(ptr) });
}
