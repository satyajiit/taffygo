// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { PolicyPage } from "@/components/PolicyPage";
import { privacyPolicy } from "@/lib/content/trust";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/privacy/");

export default function PrivacyPage() {
  return (
    <PolicyPage
      document={privacyPolicy}
    />
  );
}
