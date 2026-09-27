// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { Plus } from "lucide-react";
import { faq } from "@/lib/content/home";

/**
 * Native disclosure elements: keyboard reachable, announced by screen
 * readers, and readable with no JavaScript at all.
 */
export function Questions({ labelledBy }: { labelledBy: string }) {
  return (
    <ul aria-labelledby={labelledBy} className="border-t border-outline">
      {faq.map((item) => (
        <li key={item.question} className="border-b border-outline">
          <details className="group">
            <summary className="flex min-h-14 cursor-pointer list-none items-center justify-between gap-4 py-4 font-bold [&::-webkit-details-marker]:hidden">
              {item.question}
              <Plus
                aria-hidden="true"
                size={18}
                strokeWidth={2}
                className="shrink-0 text-secondary group-open:rotate-45"
              />
            </summary>
            <p className="max-w-[60ch] pb-6 text-secondary">{item.answer}</p>
          </details>
        </li>
      ))}
    </ul>
  );
}
