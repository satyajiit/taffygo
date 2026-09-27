// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_CORPUS_CORPUS_CANARY_H_
#define TAFFY_TEST_CORPUS_CORPUS_CANARY_H_

#include <string>
#include <vector>

// One seeded secret and the fixtures allowed to carry it.
//
// The tokens are read from the corpus at run time rather than compiled into a
// test, and that is not a stylistic preference. A test with a token typed into
// it keeps passing after the corpus rotates its canaries, and a rotation is
// exactly the moment a redaction rule keyed to the old shape stops working.
// Reading them means a rotation either keeps the suite honest or fails it.

namespace taffy::test {

struct CorpusCanary {
  CorpusCanary();
  CorpusCanary(const CorpusCanary&);
  CorpusCanary(CorpusCanary&&) noexcept;
  CorpusCanary& operator=(const CorpusCanary&);
  CorpusCanary& operator=(CorpusCanary&&) noexcept;
  ~CorpusCanary();

  // The literal token, for example a password canary.
  std::string token;
  // "password", "one-time-code", "session-token", "api-key", and so on.
  std::string secret_class;
  // Fixture identifiers, not paths.
  std::vector<std::string> carried_by;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_CORPUS_CORPUS_CANARY_H_
