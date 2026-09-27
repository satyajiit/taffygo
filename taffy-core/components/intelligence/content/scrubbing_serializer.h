// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBING_SERIALIZER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBING_SERIALIZER_H_

#include <stddef.h>

#include <string_view>

#include "taffy/components/intelligence/content/credential_field_metadata.h"
#include "taffy/components/intelligence/content/scrubbed_text.h"

class GURL;

// The one gate every log line, crash key and analytics payload in
// //taffy passes through (PAR-SEC-009, REQ-DATA-003,
// protocol section 16).
//
// The browser-process half of the seeded-secret suite. The renderer's field
// redaction omits sensitive values before they are ever serialized, which is
// where a value is supposed to stop. This class is what catches the cases that
// are not a form field at all: a URL with a token in its query, an
// Authorization header echoed into an error string, a certificate block in a
// network diagnostic, an identity token pasted into a bug report.
//
// The design in three sentences:
//
//   1. Every diagnostic sink in this component takes a ScrubbedText, and this
//      class is the only thing that can make one. Forgetting to scrub is a
//      compile error rather than a review finding.
//   2. The scanning is shape-based and holds no secrets of its own
//      (secret_shape_scanner.h).
//   3. When the caller already knows a value is sensitive, it must not pass
//      the value at all: RedactedPlaceholder takes the class and returns the
//      placeholder, so the value never enters an argument.
//
// The last point is the one worth insisting on. Scrubbing a known credential
// is a fallback, not a design: the correct call for a password is
// RedactedPlaceholder(CredentialClass::kPassword), which cannot leak because
// nothing was passed.
//
// Thread safe: every method is pure and static.

namespace taffy {

class ScrubbingSerializer {
 public:
  // Free text of unknown provenance — an error message, a header value, a
  // stack annotation.
  static ScrubbedText Serialize(std::string_view text);

  // A URL reduced to its origin. Full URLs and query strings never enter a
  // diagnostic (protocol section 16); an opaque origin serializes to the
  // literal "null", which is what url::Origin does and is unambiguous here
  // because a diagnostic is not an authorization check.
  static ScrubbedText SerializeOriginOf(const GURL& url);

  // For a value the caller already knows is sensitive. Takes the class, never
  // the value: there is nothing to leak because nothing was passed.
  static ScrubbedText RedactedPlaceholder(CredentialClass credential_class);

  // The empty string, scrubbed. Exists so a sink that needs a ScrubbedText for
  // an absent value does not have to route an empty string through the
  // scanner.
  static ScrubbedText Empty();

  // The longest input the serializer will scan. Beyond it the text is
  // truncated first, because a diagnostic that large is a payload and scanning
  // an unbounded attacker-influenced string in the browser process is a cost
  // an attacker chooses.
  static constexpr size_t kMaxScannedCharacters = 8192;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_SCRUBBING_SERIALIZER_H_
