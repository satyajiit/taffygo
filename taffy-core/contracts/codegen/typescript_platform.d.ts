// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The complete list of platform globals the generated contract TypeScript may
// use, and the reason it is a list rather than lib: ["DOM"].
//
// The generated code is the on-ramp for a WebUI or desktop surface, so it has
// to build in a browser, in a Worker and in Node alike. Compiling it against
// the whole DOM would let a generator quietly acquire a dependency on one of
// them. Compiling it against ES2022 alone would reject the two encoders every
// one of those runtimes does provide. So they are declared here, narrowly, and
// anything else a generator reaches for is a compile error in the check rather
// than a runtime error in somebody's browser.
//
// Read by taffy-core/contracts/codegen/typescript_typecheck.py. Not shipped.

declare class TextEncoder {
  encode(input?: string): Uint8Array;
}

declare class TextDecoder {
  constructor(label?: string, options?: { fatal?: boolean });
  decode(input?: ArrayBufferView | ArrayBuffer): string;
}
