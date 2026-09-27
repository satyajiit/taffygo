// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

/**
 * Why a boundary call failed. Closed, compiled-in, and content-free: a screen
 * turns a member into a sentence from a string resource, so no failure can ever
 * render text that came from a page, a provider, or an exception message.
 */
enum class FailureReason(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** The device has no network. */
    OFFLINE("offline"),

    /** The other side did not answer in time. */
    TIMED_OUT("timed_out"),

    /** The asked-for thing does not exist. */
    NOT_FOUND("not_found"),

    /** The message did not match the contract. */
    MALFORMED("malformed"),

    /** The contract version is one this build cannot read. */
    UNSUPPORTED_VERSION("unsupported_version"),

    /** A closed enumeration carried a value outside its list, so this fails closed. */
    UNSUPPORTED_VALUE("unsupported_value"),

    /** Policy did not allow it. */
    NOT_PERMITTED("not_permitted"),

    /** Storage refused the write. */
    STORAGE_REFUSED("storage_refused"),

    /** The command body did not satisfy the generated Core API contract. */
    INVALID_REQUEST("invalid_request"),

    /** The command named an earlier isolated-service generation. */
    STALE_GENERATION("stale_generation"),

    /** The command named an earlier task or workspace revision. */
    STALE_REVISION("stale_revision"),

    /** The command was not accepted before its fixed deadline. */
    DEADLINE_EXCEEDED("deadline_exceeded"),

    /** The bounded command queue had no room for this request. */
    BACKPRESSURE("backpressure"),

    /** The profile Core Service could not accept the command. */
    CORE_UNAVAILABLE("core_unavailable"),

    /** The same idempotent command was already accepted. */
    DUPLICATE("duplicate"),

    /** The browser transport returned a value outside the closed contract. */
    PROTOCOL_VIOLATION("protocol_violation"),

    /** The request named a site that no tab of the person's own shows. */
    SOURCE_NOT_OPEN("source_not_open"),

    /** The request named a site that more than one of the person's own tabs shows. */
    SOURCE_AMBIGUOUS("source_ambiguous"),

    /** There was not exactly one window in front for Taffy to work in. */
    WINDOW_UNAVAILABLE("window_unavailable"),
}
