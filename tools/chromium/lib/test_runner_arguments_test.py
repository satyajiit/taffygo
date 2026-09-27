#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Command-level regressions for test threads, displays, and argument routing.

Runs the real tools/chromium/test wrapper with temporary executable runner
stand-ins. The stand-ins record invocation; no APK, device, or build is used.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
PROFILE_SUITE = "taffy_profile_browsertests"


class ProfileBrowserRunnerArgumentsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="taffy-runner-arguments-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.marker = self.directory / "invocations"
        workspace = self.directory / "chromium"
        (workspace / "src/.git").mkdir(parents=True)
        bins = self.directory / "bin"
        bins.mkdir()
        # The wrapper's platform preflight is orthogonal to argument routing.
        # Keep this command test runnable on repository hosts without Chromium.
        for name, body in {
            "gclient": "exit 0\n",
            "uname": 'case "$1" in -m) echo x86_64;; *) echo Linux;; esac\n',
        }.items():
            command = bins / name
            command.write_text("#!/usr/bin/env bash\n" + body)
            command.chmod(0o755)
        for profile in ("dev-x64", "diag-fuzz-x64", "diag-sanitizer-x64"):
            runners = workspace / "src/out" / profile / "bin"
            runners.mkdir(parents=True)
            for suite in (PROFILE_SUITE, "taffy_unittests", "taffy_browsertests",
                          "taffy_public_test_apk", "taffy_shell_junit_tests"):
                runner = runners / ("run_" + suite)
                runner.write_text(
                    '#!/usr/bin/env bash\n'
                    'printf "%s\\n" "$@" >> "$TAFFY_TEST_RUNNER_MARKER"\n'
                    'echo "[  PASSED  ] 1 test."\n')
                runner.chmod(0o755)
        self.environment = dict(os.environ)
        self.environment.update({
            "TAFFY_ROOT": str(ROOT),
            "TAFFY_CHROMIUM_WORKSPACE": str(workspace),
            "TAFFY_TEST_RUNNER_MARKER": str(self.marker),
            "PATH": str(bins) + os.pathsep + os.environ["PATH"],
        })
        self.environment.pop("DISPLAY", None)
        self.environment.pop("WAYLAND_DISPLAY", None)

    def invoke(self, flags, suites=(PROFILE_SUITE,), profile="dev-x64"):
        self.marker.unlink(missing_ok=True)
        return subprocess.run(
            [str(ROOT / "tools/chromium/test"), "--profile", profile,
             *suites, "--", *flags],
            cwd=ROOT, env=self.environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)

    def test_scaled_profile_run_is_rejected_before_runner_invocation(self):
        for flags in (["--timeout-scale=3"], ["--timeout-scale", "3"],
                      ["--timeout-scale=.5"], ["--timeout-scale", "0"]):
            with self.subTest(flags=flags):
                result = self.invoke(flags)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(self.marker.exists(), result.stdout)
                self.assertIn("off the Android UI thread", result.stdout)
                self.assertIn("Omit --timeout-scale", result.stdout)

    def test_default_and_unit_scales_preserve_the_runner_arguments(self):
        for flags in ([], ["--timeout-scale=1"], ["--timeout-scale", "1.0"],
                      ["--timeout-scale=1e0"]):
            with self.subTest(flags=flags):
                result = self.invoke(flags)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertTrue(self.marker.exists(), result.stdout)
                if flags:
                    self.assertEqual(self.marker.read_text().splitlines(), flags)

    def test_default_suite_selection_also_rejects_scaled_profile_run(self):
        result = self.invoke(["--timeout-scale=3"], suites=())
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(self.marker.exists(), result.stdout)

    def test_other_suites_and_host_profiles_keep_existing_behavior(self):
        for suites, profile in ((("taffy_unittests",), "dev-x64"),
                                ((PROFILE_SUITE,), "diag-fuzz-x64")):
            with self.subTest(suites=suites, profile=profile):
                result = self.invoke(["--timeout-scale=3"], suites, profile)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(self.marker.read_text().splitlines()[-1],
                                 "--timeout-scale=3")

    def test_headless_host_cpp_suites_choose_the_virtual_display_backend(self):
        flags = ["--gtest_filter=SomeTest.*", "--test-launcher-retry-limit=0"]
        for suite in ("taffy_unittests", "taffy_browsertests"):
            with self.subTest(suite=suite):
                result = self.invoke(flags, (suite,), "diag-sanitizer-x64")
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(self.marker.read_text().splitlines(), [
                    "--use-xvfb", "--asan-detect-odr-violation=0", *flags])

    def test_explicit_host_display_choices_are_not_overridden(self):
        for flags in (["--no-xvfb"], ["--use-xvfb"],
                      ["--no-xvfb", "--use-weston"],
                      ["--no-xvfb", "--use-mutter"]):
            with self.subTest(flags=flags):
                result = self.invoke(flags, ("taffy_unittests",), "diag-fuzz-x64")
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(self.marker.read_text().splitlines(), [
                    "--asan-detect-odr-violation=0", *flags])

    def test_android_and_jvm_runners_receive_no_display_argument(self):
        flags = ["--test-launcher-retry-limit=0"]
        for suite, profile in (("taffy_unittests", "dev-x64"),
                               ("taffy_browsertests", "dev-x64"),
                               ("taffy_shell_junit_tests", "diag-fuzz-x64")):
            with self.subTest(suite=suite, profile=profile):
                result = self.invoke(flags, (suite,), profile)
                self.assertEqual(result.returncode, 0, result.stdout)
                expected = flags if profile == "dev-x64" else [
                    "--asan-detect-odr-violation=0", *flags]
                self.assertEqual(self.marker.read_text().splitlines(), expected)


if __name__ == "__main__":
    unittest.main()
