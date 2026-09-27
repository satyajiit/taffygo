#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Runs kapt over Kotlin sources and emits a srcjar of the generated Java.

This is the action behind `taffy_dagger_library` (../taffy_dagger_library.gni) and
has no other caller.  Read that file first: it explains the target shape, this
one explains the command.

WHY THIS EXISTS.  Chromium runs annotation processors in Turbine, and
`build/android/gyp/turbine.py` filters its input to `.java` files only, so an
annotation written on a Kotlin declaration is invisible to every processor.
kapt closes that gap: it generates Java stubs from the Kotlin sources, runs
javac's annotation processing over the stubs, and writes the generated Java out.

WHY IT IS NOT A PATCH.  kapt ships inside Chromium's own pinned kotlinc
(`third_party/kotlinc/current/lib/kotlin-annotation-processing.jar`, and a
`bin/kapt` launcher beside `bin/kotlinc`).  Chromium's build simply never calls
it.  This script calls it from an ordinary GN `action`, so nothing upstream is
edited: the generated srcjar is handed to an ordinary `android_library` through
`srcjar_deps`, which every Chromium Java target already accepts.

TWO MECHANICS THAT COST AN AFTERNOON EACH, RECORDED SO THEY ARE NOT REDISCOVERED:

  * kotlinc splits every `-P` plugin option value on commas.  A processor list
    passed as `processors=a.B,c.D` therefore arrives as one valid option and one
    argument called `c.D`, and kotlinc fails with
    `wrong plugin option format: null`.  Every option here is passed as its own
    repeated `-P`, and a comma in a value is an assertion failure rather than a
    confusing error later.
  * kapt hands its classpath to javac, and javac must be able to resolve
    `kotlin.Metadata` in full.  Without kotlin-stdlib on that classpath the
    processors start, reach `getElementsAnnotatedWith`, and die with
    `IncompleteAnnotationException: kotlin.Metadata missing element mv`.  Under
    GN this is free: `java_library_impl` already asserts that every target
    carrying a `.kt` source depends on `//third_party/kotlin_stdlib`, so the
    stdlib is always in `javac_full_interface_classpath`.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile

_KAPT_PLUGIN_ID = 'org.jetbrains.kotlin.kapt3'


def _plugin_opts(name, values):
  """Repeated `-P` pairs. Values may not contain commas (see the module docs)."""
  out = []
  for value in values:
    assert ',' not in value, (
        'kotlinc splits -P values on commas; %s=%s would be mis-parsed' %
        (name, value))
    out += ['-P', 'plugin:%s:%s=%s' % (_KAPT_PLUGIN_ID, name, value)]
  return out


def _read_classpath(build_config, keys):
  with open(build_config, encoding='utf-8') as fp:
    config = json.load(fp)
  entries = []
  for key in keys:
    entries += config.get(key, [])
  # Preserve order, drop duplicates: the SDK jars must stay first.
  seen = set()
  ordered = []
  for entry in entries:
    if entry not in seen:
      seen.add(entry)
      ordered.append(entry)
  return ordered


def _write_srcjar(sources_dir, srcjar_path):
  count = 0
  with zipfile.ZipFile(srcjar_path, 'w', zipfile.ZIP_STORED) as out:
    for root, _, files in os.walk(sources_dir):
      for name in sorted(files):
        if not name.endswith(('.java', '.kt')):
          continue
        full = os.path.join(root, name)
        arcname = os.path.relpath(full, sources_dir)
        info = zipfile.ZipInfo(arcname, date_time=(1980, 1, 1, 0, 0, 0))
        info.external_attr = 0o644 << 16
        with open(full, 'rb') as fp:
          out.writestr(info, fp.read())
        count += 1
  return count


def main(argv):
  parser = argparse.ArgumentParser()
  parser.add_argument('--kotlinc', required=True)
  parser.add_argument('--kapt-plugin-jar', required=True)
  parser.add_argument('--java-home', required=True)
  parser.add_argument('--build-config', required=True,
                      help='The consuming target\'s .javac.build_config.json; '
                           'its javac_full_interface_classpath is the classpath '
                           'kapt compiles the stubs against.')
  parser.add_argument('--processor-jar', action='append', default=[])
  parser.add_argument('--processor', action='append', default=[])
  parser.add_argument('--work-dir', required=True)
  parser.add_argument('--srcjar', required=True)
  parser.add_argument('--source', action='append', default=[])
  args = parser.parse_args(argv)

  work = os.path.abspath(args.work_dir)
  shutil.rmtree(work, ignore_errors=True)
  gen_sources = os.path.join(work, 'sources')
  gen_classes = os.path.join(work, 'classes')
  gen_stubs = os.path.join(work, 'stubs')
  compile_out = os.path.join(work, 'compiled')
  for path in (gen_sources, gen_classes, gen_stubs, compile_out):
    os.makedirs(path)

  classpath = _read_classpath(args.build_config,
                              ['sdk_interface_jars',
                               'javac_full_interface_classpath'])

  cmd = [args.kotlinc]
  cmd += [
      '-jvm-target', '11',
      '-no-jdk',
      '-no-stdlib',
      '-no-reflect',
      '-J-Xmx2G',
      '-Xplugin=' + args.kapt_plugin_jar,
  ]
  cmd += _plugin_opts('aptMode', ['stubsAndApt'])
  cmd += _plugin_opts('sources', [gen_sources])
  cmd += _plugin_opts('classes', [gen_classes])
  cmd += _plugin_opts('stubs', [gen_stubs])
  cmd += _plugin_opts('correctErrorTypes', ['true'])
  cmd += _plugin_opts('processors', args.processor)
  cmd += _plugin_opts('apclasspath', [os.path.abspath(j)
                                      for j in args.processor_jar])
  cmd += ['-classpath', os.pathsep.join(os.path.abspath(c) for c in classpath)]
  cmd += ['-d', compile_out]
  cmd += [os.path.abspath(s) for s in args.source]

  env = os.environ.copy()
  env['JAVA_HOME'] = os.path.abspath(args.java_home)
  env.pop('JAVA_TOOL_OPTIONS', None)

  proc = subprocess.run(cmd, env=env, capture_output=True, text=True,
                        check=False)
  noise = ('warning: unknown enum constant', 'reason: class file for')
  stderr = '\n'.join(line for line in proc.stderr.splitlines()
                     if not any(n in line for n in noise))
  if proc.returncode != 0:
    sys.stderr.write(proc.stdout)
    sys.stderr.write(stderr + '\n')
    sys.stderr.write('\nkapt failed. Command was:\n  %s\n' % ' '.join(cmd))
    return proc.returncode
  if stderr.strip():
    sys.stderr.write(stderr + '\n')

  count = _write_srcjar(gen_sources, args.srcjar)
  print('kapt generated %d file(s) into %s' % (count, args.srcjar))
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
