// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useEffect, useRef } from "react";
import { ArrowUpRight, ChevronDown, Menu, Star } from "lucide-react";
import { siteHref, withBasePath } from "@/lib/base-path";
import { header } from "@/lib/content";
import { links } from "@/lib/site";
import { ThemeSwitch } from "./ThemeSwitch";

const explore = [
  { href: "/product/", title: "The browser", text: "Follow a task in the Android app." },
  { href: "/use-cases/", title: "Try the demos", text: "Documents, forms, and file preparation." },
  { href: "/providers/", title: "AI models", text: "Search providers and model capabilities." },
  { href: "/technology/", title: "Architecture", text: "Chromium, Rust, and on-device Python." },
  { href: "/built-for-phones/", title: "App screens", text: "Browse the light and dark interfaces." },
  { href: "/privacy/", title: "Privacy", text: "Where your data stays and where it goes." },
];

/** Floating navigation with a native disclosure menu on narrow screens. */
export function Header() {
  const menu = useRef<HTMLDetailsElement>(null);
  useEffect(() => {
    const close = (event: PointerEvent) => {
      if (event.target instanceof Node && !menu.current?.contains(event.target)) menu.current?.removeAttribute("open");
    };
    document.addEventListener("pointerdown", close);
    return () => document.removeEventListener("pointerdown", close);
  }, []);
  return (
    <header className="studio-header">
      <div className="container-site nav-bar">
        <a href={withBasePath("/")} aria-label={header.homeLabel} className="brand-link">
          {["light", "dark"].map(variant => <img key={variant}
            src={withBasePath(`/brand/taffygo-lockup-color-on-${variant}-118@2x.webp`)}
            width={118} height={59} alt={header.lockupAlt} className={`lockup lockup-${variant}`} />)}
        </a>
        <nav aria-label={header.navLabel} className="nav-center">
          <details ref={menu} className="explore-menu" onKeyDown={event => {
            if (event.key === "Escape") { menu.current?.removeAttribute("open"); menu.current?.querySelector("summary")?.focus(); }
          }}>
            <summary aria-label="Explore TaffyGo"><span className="desktop-label">Explore TaffyGo</span><Menu className="mobile-label" size={20} /><ChevronDown size={14} /></summary>
            <div className="mega-panel">
              <div className="mega-links">{explore.map(item => <a key={item.href} href={withBasePath(item.href)}><strong>{item.title}<ArrowUpRight size={16} /></strong><span>{item.text}</span></a>)}</div>
              <div className="menu-theme"><ThemeSwitch /></div>
            </div>
          </details>
          <a className="nav-direct" href={withBasePath("/providers/")}>AI models</a>
          <a className="nav-direct" href={withBasePath("/use-cases/")}>Demos</a>
        </nav>
        <div className="nav-actions">
          <a href={links.repository} className="nav-github"><Star size={16} /> GitHub</a>
          <div className="nav-theme"><ThemeSwitch /></div>
          <a href={siteHref(header.getHref)} className="btn btn-ink btn-compact"><span className="desktop-label">Get TaffyGo</span><span className="mobile-label">Get it</span><ArrowUpRight size={16} /></a>
        </div>
      </div>
    </header>
  );
}
