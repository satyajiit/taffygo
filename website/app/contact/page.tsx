// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { siteHref } from "@/lib/base-path";
import { contact } from "@/lib/content/contact";
import { pageMetadata } from "@/lib/site";

export const metadata = pageMetadata("/contact/");

/**
 * Every way to reach the people who make TaffyGo leads to the public
 * repository. The page collects nothing: no form, no mailbox, no script.
 */
export default function ContactPage() {
  return (
    <article aria-labelledby="contact-title" className="container-site pt-[var(--space-xl)] pb-[var(--space-3xl)]">
      <h1 id="contact-title" className="type-display">
        {contact.title}
      </h1>
      <p className="type-lead mt-6 max-w-[36rem] text-secondary">{contact.standfirst}</p>

      <ul className="mt-[var(--space-2xl)] border-t border-outline">
        {contact.routes.map((route) => (
          <li
            key={route.href}
            className="grid gap-3 border-b border-outline py-6 md:grid-cols-[minmax(0,2fr)_minmax(0,1fr)] md:items-center md:gap-8"
          >
            <div className="min-w-0">
              <h2 className="type-h3">{route.topic}</h2>
              <p className="mt-2 max-w-[48ch] text-secondary">{route.body}</p>
            </div>
            <a href={siteHref(route.href)} className="link-arrow md:justify-self-end">
              {route.action}
            </a>
          </li>
        ))}
      </ul>
    </article>
  );
}
