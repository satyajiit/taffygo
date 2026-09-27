// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import java.io.Closeable

/**
 * Browser-owned endpoint that carries a form description down and the values a
 * person typed up, and nothing else.
 *
 * The same shape as [BrowserCoreApiEndpoint] and for the same reason: Chromium
 * supplies this interface with the Profile component, because only the browser
 * process owns the vault a value is minted in and the page the value is typed
 * into. There is no default or preview implementation in the product graph —
 * [UnavailableTaskInputEndpoint] is the closed answer, not a stand-in that
 * pretends to work.
 *
 * It is a separate endpoint rather than another method on
 * [BrowserCoreApiEndpoint] because the two carry different things and must be
 * able to be absent independently: a build can have a working Core API and no
 * vault behind it, and in that build every surface that reads a task must go on
 * working while every surface that would collect a value must not appear.
 */
interface BrowserTaskInputEndpoint : TaskInputClient, Closeable
