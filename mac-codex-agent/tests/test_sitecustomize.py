import pathlib
import sys
import unittest

AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from sitecustomize import bundled_resource_paths, should_launch_bundled_app


class BundledSiteCustomizeTests(unittest.TestCase):
    def test_resource_paths_follow_the_moved_bundle(self):
        executable = pathlib.Path(
            "/Volumes/Agent Release/StopWatch Codex Agent.app/Contents/MacOS/"
            "StopWatchCodexAgent"
        )

        self.assertEqual(
            bundled_resource_paths(executable),
            (
                pathlib.Path(
                    "/Volumes/Agent Release/StopWatch Codex Agent.app/Contents/"
                    "Resources/app"
                ),
                pathlib.Path(
                    "/Volumes/Agent Release/StopWatch Codex Agent.app/Contents/"
                    "Resources/site-packages"
                ),
            ),
        )

    def test_only_bare_bundle_execution_launches_the_menu_app(self):
        executable = pathlib.Path(
            "/Applications/StopWatch Codex Agent.app/Contents/MacOS/"
            "StopWatchCodexAgent"
        )

        self.assertTrue(should_launch_bundled_app(executable, [""]))
        self.assertFalse(should_launch_bundled_app(executable, ["-c"]))
        self.assertFalse(
            should_launch_bundled_app(
                executable.with_name("StopWatchAgentTestRuntime"), [""]
            )
        )


if __name__ == "__main__":
    unittest.main()
