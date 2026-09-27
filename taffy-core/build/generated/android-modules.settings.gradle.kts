// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from taffy-core/build/components.toml. Do not edit.
fun registerTaffyAndroidModule(path: String) {
    include(path)
    var current = ""
    for (segment in path.removePrefix(":").split(':')) {
        current += ":$segment"
        project(current).projectDir =
            rootDir.resolve("taffy-core/ui/android/${current.removePrefix(":").replace(':', '/')}")
    }
}

registerTaffyAndroidModule(":app")
registerTaffyAndroidModule(":core:analytics")
registerTaffyAndroidModule(":core:api")
registerTaffyAndroidModule(":core:assets")
registerTaffyAndroidModule(":core:browser")
registerTaffyAndroidModule(":core:common")
registerTaffyAndroidModule(":core:credentials")
registerTaffyAndroidModule(":core:designsystem")
registerTaffyAndroidModule(":core:model")
registerTaffyAndroidModule(":core:page-intelligence")
registerTaffyAndroidModule(":core:preferences")
registerTaffyAndroidModule(":core:providerauth")
registerTaffyAndroidModule(":core:providers")
registerTaffyAndroidModule(":core:task")
registerTaffyAndroidModule(":core:ui")
registerTaffyAndroidModule(":core:workspace")
registerTaffyAndroidModule(":feature:assistant")
registerTaffyAndroidModule(":feature:browsing")
registerTaffyAndroidModule(":feature:downloads")
registerTaffyAndroidModule(":feature:onboarding")
registerTaffyAndroidModule(":feature:providers")
registerTaffyAndroidModule(":feature:settings")
registerTaffyAndroidModule(":feature:workspaces")
