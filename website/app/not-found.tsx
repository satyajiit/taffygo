// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { Metadata } from "next";
import { withBasePath } from "@/lib/base-path";
import { notFound } from "@/lib/content";

export const metadata: Metadata = {
  title: { absolute: `${notFound.title} | TaffyGo` },
  robots: { index: false, follow: true },
};

export default function NotFound() {
  return (
    <div className="container-site pt-[var(--space-xl)] pb-[var(--space-4xl)]">
      <h1 className="type-display">{notFound.title}</h1>
      <p className="type-lead mt-6 max-w-[36rem] text-secondary">{notFound.body}</p>
      <a href={withBasePath("/")} className="btn btn-ink mt-8">
        {notFound.homeLabel}
      </a>
    </div>
  );
}
