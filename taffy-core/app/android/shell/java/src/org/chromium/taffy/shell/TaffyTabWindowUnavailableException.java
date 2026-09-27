// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

/** Clean refusal when Chromium cannot assign another registered product window. */
final class TaffyTabWindowUnavailableException extends IllegalStateException {
    TaffyTabWindowUnavailableException() {
        super("No Chromium tab window is available for this Activity");
    }
}
