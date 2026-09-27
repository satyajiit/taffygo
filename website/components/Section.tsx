// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { ReactNode } from "react";

/**
 * One labelled landmark per page section: the heading names the region, so a
 * screen reader's landmark list reads like the page's contents. Sections set
 * their own spacing and surface, so the page does not fall into one rhythm.
 */
export function Section({
  id,
  title,
  lead,
  className = "",
  headClassName = "",
  children,
}: {
  id: string;
  title: string;
  lead?: string;
  className?: string;
  headClassName?: string;
  children: ReactNode;
}) {
  const headingId = `${id}-title`;

  return (
    <section id={id} aria-labelledby={headingId} className={`scroll-mt-6 ${className}`}>
      <div className="container-site">
        <div className={`max-w-[42rem] ${headClassName}`}>
          <h2 id={headingId} className="type-h2">
            {title}
          </h2>
          {lead ? <p className="type-lead mt-4 text-secondary">{lead}</p> : null}
        </div>
        {children}
      </div>
    </section>
  );
}
