// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type { ReactNode } from "react";
import { siteHref } from "@/lib/base-path";
import { contentsLabel, type DocBlock, type Inline, type TrustDocument } from "@/lib/content/trust";
import { Painting, type ArtName } from "./PageHead";

/** A run of text in which some parts are links. */
export function InlineText({ parts }: { parts: ReadonlyArray<Inline> }) {
  return (
    <>
      {parts.map((part, index) =>
        typeof part === "string" ? (
          part
        ) : (
          <a key={`${part.href}-${index}`} href={siteHref(part.href)}>
            {part.label}
          </a>
        ),
      )}
    </>
  );
}

function Block({ block }: { block: DocBlock }) {
  if (block.kind === "p") {
    return (
      <p>
        <InlineText parts={block.text} />
      </p>
    );
  }
  const List = block.kind;
  return (
    <List>
      {block.items.map((item, index) => (
        <li key={index}>
          <InlineText parts={item} />
        </li>
      ))}
    </List>
  );
}

/**
 * One standing document, set as a single column at a reading measure: the
 * title, a standfirst that answers the page's question in two or three
 * sentences, and then the sections. From 64rem a contents list sits to the
 * left and stays in view. `children` follow the last section.
 */
export function PolicyPage({
  document,
  art,
  children,
}: {
  document: TrustDocument;
  art?: { name: ArtName; alt: string };
  children?: ReactNode;
}) {
  return (
    <article
      aria-labelledby="doc-title"
      className="container-site pt-[var(--space-xl)] pb-[var(--space-3xl)]"
    >
      <div
        className={
          art
            ? "grid items-center gap-[var(--space-xl)] lg:grid-cols-[minmax(0,6fr)_minmax(0,5fr)] lg:gap-[var(--space-3xl)]"
            : ""
        }
      >
        <header className="min-w-0 max-w-[var(--measure)]">
          <h1 id="doc-title" className="type-display">
            {document.title}
          </h1>
          <p className="type-lead mt-6 text-secondary">{document.standfirst}</p>
          {document.updated ? (
            <p className="type-small mt-4 text-secondary">{document.updated}</p>
          ) : null}
        </header>
        {art ? (
          <Painting name={art.name} alt={art.alt} sizes="(min-width: 60rem) 40vw, 100vw" />
        ) : null}
      </div>

      <div className="mt-[var(--space-2xl)] grid gap-[var(--space-xl)] lg:grid-cols-[minmax(0,14rem)_minmax(0,1fr)] lg:gap-[var(--space-3xl)]">
        <nav aria-labelledby="doc-contents" className="hidden lg:block">
          <div className="sticky top-6">
            <p id="doc-contents" className="type-small font-bold text-secondary">
              {contentsLabel}
            </p>
            <ul className="mt-3 grid gap-1">
              {document.sections.map((section) => (
                <li key={section.id}>
                  <a
                    href={`#${section.id}`}
                    className="type-small block py-1 text-secondary hover:text-primary"
                  >
                    {section.heading}
                  </a>
                </li>
              ))}
            </ul>
          </div>
        </nav>

        <div className="prose-doc min-w-0">
          {document.sections.map((section) => (
            <section key={section.id} id={section.id} aria-labelledby={`${section.id}-h`} className="scroll-mt-6">
              <h2 id={`${section.id}-h`}>{section.heading}</h2>
              {section.blocks.map((block, index) => (
                <Block key={index} block={block} />
              ))}
            </section>
          ))}
          {children}
        </div>
      </div>
    </article>
  );
}
