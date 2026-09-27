// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/** Tailwind is configured CSS-first in tailwind.css; this only wires the plugin. */
const config = {
  plugins: {
    "@tailwindcss/postcss": {},
  },
};

export default config;
