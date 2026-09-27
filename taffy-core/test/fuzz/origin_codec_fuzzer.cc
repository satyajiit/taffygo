// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "base/check.h"
#include "base/check_op.h"

#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/test/test_timeouts.h"
#include "content/public/test/browser_task_environment.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "url/gurl.h"
#include "url/origin.h"

// URL and origin canonicalization, which is where a comparison that looks
// correct stops being correct.
//
// The property under test is the one the codec exists for: an opaque origin is
// never equal to another opaque origin unless their identifiers match. A
// comparison on serializations would make every opaque origin equal to every
// other, and an action authorized against one sandboxed document would pass a
// check against a different one.

// The codec asserts DCHECK_CURRENTLY_ON(BrowserThread::UI), which is a true
// statement about where it runs and not something to relax for a fuzzer. A
// fuzzer has no browser threads unless it makes them, so the very first input
// aborted the process before reaching any of the property below. One browser
// task environment for the process is exactly what the assertion asks for.
struct OriginCodecEnvironment {
  OriginCodecEnvironment() {
    if (!base::CommandLine::InitializedForCurrentProcess()) {
      base::CommandLine::Init(0, nullptr);
    }
    // The task environment reads the test timeouts, which a test binary's main
    // initializes and a fuzzer's does not; without this the first input dies
    // on DCHECK initialized_ rather than on anything the property says. It
    // reads the command line, so it goes after the line above.
    TestTimeouts::Initialize();
    task_environment = std::make_unique<content::BrowserTaskEnvironment>();
  }

  base::AtExitManager at_exit_manager;
  std::unique_ptr<content::BrowserTaskEnvironment> task_environment;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static OriginCodecEnvironment environment;
  const std::string text(reinterpret_cast<const char*>(data), size);
  const GURL url(text);
  const url::Origin origin = url::Origin::Create(url);

  taffy::OriginCodec& codec = taffy::OriginCodec::Get();
  const taffy::Origin wire = codec.ToWireOrigin(origin);
  CHECK(codec.Matches(wire, origin));

  if (wire.is_opaque()) {
    taffy::Origin other = wire;
    other.opaque_id += "-different";
    CHECK(!(other == wire));
  }
  codec.ClearForTesting();
  return 0;
}
