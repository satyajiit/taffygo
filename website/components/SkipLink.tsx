// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/**
 * The first thing a keyboard reaches on every page. Hidden by transform
 * rather than by `display`, so it stays in the focus order.
 */
export function SkipLink() {
  return (
    <a
      href="#main"
      className="skip-link rounded-full bg-primary px-5 py-3 font-bold text-surface shadow-lg"
    >
      Skip to content
    </a>
  );
}
