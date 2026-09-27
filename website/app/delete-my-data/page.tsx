// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { PolicyPage } from "@/components/PolicyPage";
import { deleteMyData } from "@/lib/content/trust";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/delete-my-data/");

export default function DeleteMyDataPage() {
  return (
    <PolicyPage
      document={deleteMyData}
    />
  );
}
