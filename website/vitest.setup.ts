// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import "@testing-library/jest-dom/vitest";
import { cleanup } from "@testing-library/react";
import { afterEach, vi } from "vitest";

// next/font/local is a build-time transform that only the Next.js compiler
// runs. Under vitest the layout gets the same shape back: a class that sets
// the font variable.
vi.mock("next/font/local", () => ({
  default: () => ({
    className: "font-grotesk",
    variable: "font-grotesk-variable",
    style: { fontFamily: "Space Grotesk" },
  }),
}));

afterEach(() => {
  cleanup();
});
