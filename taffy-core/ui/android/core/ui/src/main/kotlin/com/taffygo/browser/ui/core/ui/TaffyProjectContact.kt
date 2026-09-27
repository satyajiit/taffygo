// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import java.net.URLEncoder

/**
 * The two places a person can reach the people who make TaffyGo, each written
 * once (decision 0253).
 *
 * TaffyGo runs no server (decision 0200), so a report or a piece of feedback
 * never leaves the phone from here. What this builds is an address: a
 * `mailto:` draft the person's own email app opens and the person sends, or a
 * page on the public repository that opens in a tab. Changing either
 * destination is one edit to this file.
 */
object TaffyProjectContact {
    /** The mailbox a report or feedback draft is addressed to. */
    const val EMAIL: String = "admin@matterwardlabs.com"

    /** The public repository this build's source is published in (decisions 0206 and 0251). */
    const val REPOSITORY: String = "https://github.com/satyajiit/taffygo"

    /**
     * GitHub's list of the ways to open an issue. General feedback goes here,
     * because the repository's templates ask the questions and this build
     * should not guess which one fits.
     */
    const val ISSUE_CHOOSER: String = "$REPOSITORY/issues/new/choose"

    /**
     * The repository's bug form. The repository turns blank issues off, so a
     * new-issue address that names no form drops the title it was given.
     */
    private const val ISSUE_FORM: String = "bug_report.yml"

    /**
     * A `mailto:` address to [EMAIL] carrying [subject] and [body].
     *
     * Every value is percent-encoded as UTF-8, and a line break in the body is
     * written as `%0D%0A`, which is what RFC 6068 asks of a body.
     */
    fun emailDraftUri(subject: String, body: String): String =
        "mailto:$EMAIL?subject=${encode(subject)}&body=${encode(crlf(body))}"

    /**
     * The bug form on the public repository, with [title] filled in and
     * nothing else. An issue is public, so a caller passes a title and never
     * what Taffy said.
     */
    fun newIssueAddress(title: String): String =
        "$REPOSITORY/issues/new?template=$ISSUE_FORM&title=${encode(title)}"

    private fun crlf(text: String): String = text.replace("\r\n", "\n").replace("\n", "\r\n")

    // URLEncoder writes form encoding, where a space is `+`. A literal plus has
    // already become %2B by then, so every remaining `+` is a space.
    private fun encode(value: String): String =
        URLEncoder.encode(value, Charsets.UTF_8.name()).replace("+", "%20")
}
