// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import tokens from "../../taffy-core/resources/tokens/tokens.json";

/** Convert the app token source's ARGB32 form to a CSS hex colour. */
function argbToCss(value: string): string {
  const alpha = value.slice(0, 2);
  const rgb = value.slice(2);
  return `#${rgb}${alpha.toUpperCase() === "FF" ? "" : alpha}`;
}

/** Browser-chrome colours derived from the same source as the app UI. */
export const appTheme = {
  lightSurface: argbToCss(tokens.themes.light.surface),
  darkSurface: argbToCss(tokens.themes.dark.surface),
} as const;
