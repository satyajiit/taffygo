// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.settings.AboutRepository
import org.chromium.base.ApkInfo
import org.chromium.base.version_info.VersionInfo

/**
 * Reads the two version facts of this exact APK.
 *
 * It holds nothing open. Screen SCR-408 once also listed the pinned revision,
 * the downstream patch count, the management posture and an index of the
 * packaged credits file; the notices moved to the website and the rest were
 * facts about the build rather than about the browsing (decision 0127), so what
 * is left needs no profile, no scope and no temporary file.
 */
internal class ChromiumAboutRepository : AboutRepository {
    override val facts = AboutRepository.Facts(
        versionName = factOrNull(ApkInfo::getPackageVersionName),
        chromiumVersion = factOrNull(VersionInfo::getProductVersion),
    )

    private companion object {
        fun factOrNull(read: () -> String): String? = try {
            read().trim().takeIf(String::isNotEmpty)
        } catch (_: RuntimeException) {
            null
        }
    }
}
