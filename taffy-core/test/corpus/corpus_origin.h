// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_ORIGIN_H_
#define TAFFY_TEST_CORPUS_CORPUS_ORIGIN_H_

#include <stdint.h>

#include <string>

// One of the corpus's web origins.
//
// The corpus is served over four separate origins because most of what the
// protocol has to get right is invisible on a single-origin page: frame
// inclusion policy, out-of-process iframes, cross-origin redirects, popup
// ownership, and the exfiltration sink all need a second and a third site that
// the browser genuinely treats as different.
//
// `port_offset` is how the corpus's own server assigns ports when it runs one
// port per origin. A browser test does not use it — an embedded test server
// picks its own port — but it is read anyway so that the map from origin key
// to server has exactly one definition, in the corpus, rather than one here
// and one in test-fixtures/web/serve.py.

namespace taffy::test {

struct CorpusOrigin {
  CorpusOrigin();
  CorpusOrigin(const CorpusOrigin&);
  CorpusOrigin(CorpusOrigin&&) noexcept;
  CorpusOrigin& operator=(const CorpusOrigin&);
  CorpusOrigin& operator=(CorpusOrigin&&) noexcept;
  ~CorpusOrigin();

  // "primary", "partner", "embed", "hostile".
  std::string key;
  // The hostname the corpus's own server uses, for example a name under the
  // reserved .test top-level domain.
  std::string hostname;
  // What the corpus says this origin is for. Carried so a failure message can
  // explain which site it is talking about.
  std::string role;
  uint32_t port_offset = 0;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_ORIGIN_H_
