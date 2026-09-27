// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.buildlogic

import java.io.File

/** One exact Gradle-module projection generated from `components.toml`. */
internal data class GeneratedModule(
    val path: String,
    val component: String,
    val layer: Int,
    val projectDirectory: String,
    val apiSurface: String,
    val apiDependencies: Set<String>,
    val implementationDependencies: Set<String>,
)

/**
 * The Gradle view of the authoritative Taffy component graph.
 *
 * This parser deliberately accepts only the committed seven-column projection.
 * Build scripts cannot add an edge or reinterpret its visibility, and the
 * `layer` column is held here too: an edge that does not fall strictly is a
 * configuration error rather than a link order that happens to work.
 */
internal class GeneratedModuleGraph private constructor(
    private val modules: Map<String, GeneratedModule>,
) {
    fun requireModule(path: String): GeneratedModule = modules[path]
        ?: error("$path is absent from the generated component graph ($TABLE)")

    companion object {
        const val TABLE = "taffy-core/build/generated/android-modules.tsv"

        fun load(table: File): GeneratedModuleGraph {
            require(table.isFile) { "the generated component graph is missing: $table" }
            val modules = linkedMapOf<String, GeneratedModule>()
            table.readLines().forEachIndexed { index, raw ->
                if (raw.isBlank() || raw.startsWith('#')) return@forEachIndexed
                val fields = raw.split('\t')
                require(fields.size == 7) {
                    "${table.name}:${index + 1}: expected 7 tab-separated columns, got ${fields.size}"
                }
                val path = fields[0]
                val component = fields[1]
                val layer = fields[2].toIntOrNull()
                val directory = fields[3]
                val surface = fields[4]
                val apiRaw = fields[5]
                val implementationRaw = fields[6]
                require(layer != null && layer > 0) {
                    "${table.name}:${index + 1}: layer must be a positive integer, got ${fields[2]}"
                }
                require(MODULE_PATH.matches(path)) {
                    "${table.name}:${index + 1}: invalid Gradle path $path"
                }
                require(component.isNotBlank()) {
                    "${table.name}:${index + 1}: component id is empty"
                }
                require(surface == "flat" || surface == "split") {
                    "${table.name}:${index + 1}: api surface must be flat or split"
                }
                val module = GeneratedModule(
                    path = path,
                    component = component,
                    layer = layer,
                    projectDirectory = directory,
                    apiSurface = surface,
                    apiDependencies = dependencies(apiRaw, table, index),
                    implementationDependencies = dependencies(implementationRaw, table, index),
                )
                require(module.apiDependencies.intersect(module.implementationDependencies).isEmpty()) {
                    "${table.name}:${index + 1}: an edge cannot be both api and implementation"
                }
                require(modules.put(path, module) == null) {
                    "${table.name}:${index + 1}: duplicate module $path"
                }
            }
            require(modules.isNotEmpty()) { "${table.name}: no Android modules" }
            modules.values.forEach { module ->
                (module.apiDependencies + module.implementationDependencies).forEach { dependency ->
                    require(dependency in modules) {
                        "${table.name}: ${module.path} names unknown module $dependency"
                    }
                    require(dependency != module.path) {
                        "${table.name}: ${module.path} depends on itself"
                    }
                    require(modules.getValue(dependency).layer < module.layer) {
                        "${table.name}: ${module.path} at layer ${module.layer} depends on " +
                            "$dependency at layer ${modules.getValue(dependency).layer}; " +
                            "every edge falls strictly"
                    }
                }
            }
            return GeneratedModuleGraph(modules)
        }

        private fun dependencies(raw: String, table: File, index: Int): Set<String> {
            if (raw.isEmpty()) return emptySet()
            val values = raw.split(',')
            require(values.all(MODULE_PATH::matches)) {
                "${table.name}:${index + 1}: invalid dependency list $raw"
            }
            require(values == values.distinct().sorted()) {
                "${table.name}:${index + 1}: dependencies must be unique and sorted"
            }
            return values.toSet()
        }

        private val MODULE_PATH = Regex("^:[a-z][a-z0-9-]*(?::[a-z][a-z0-9-]*)*$")
    }
}
