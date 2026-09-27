// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_expected_field.h"

namespace taffy::test {

CorpusExpectedField::CorpusExpectedField() = default;
CorpusExpectedField::CorpusExpectedField(const CorpusExpectedField&) = default;
CorpusExpectedField::CorpusExpectedField(CorpusExpectedField&&) noexcept =
    default;
CorpusExpectedField& CorpusExpectedField::operator=(
    const CorpusExpectedField&) = default;
CorpusExpectedField& CorpusExpectedField::operator=(
    CorpusExpectedField&&) noexcept = default;
CorpusExpectedField::~CorpusExpectedField() = default;

}  // namespace taffy::test
