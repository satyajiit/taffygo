// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/correctness/form_action_test_base.h"

namespace taffy::test {

FormActionTestBase::FormActionTestBase()
    : TaffyObservationTestBase(FixtureOriginMap::Scheme::kHttps) {}

FormActionTestBase::~FormActionTestBase() = default;

void FormActionTestBase::SetUpOnMainThread() {
  TaffyObservationTestBase::SetUpOnMainThread();
  values_.BeginGeneration("test-profile", 1u);
  service()->SetValueReferenceVault(&values_);
}

void FormActionTestBase::TearDownOnMainThread() {
  service()->SetValueReferenceVault(nullptr);
  TaffyObservationTestBase::TearDownOnMainThread();
}

}  // namespace taffy::test
