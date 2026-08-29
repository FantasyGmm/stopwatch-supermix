"""Bootstrap the relocatable menu application from its embedded Python."""

import os
import pathlib
import site
import sys


APP_EXECUTABLE_NAME = "StopWatchCodexAgent"


def bundled_resource_paths(executable: pathlib.Path):
    contents = executable.resolve().parent.parent
    resources = contents / "Resources"
    return resources / "app", resources / "site-packages"


def should_launch_bundled_app(executable: pathlib.Path, argv) -> bool:
    override = os.environ.get("STOPWATCH_CODEX_APP")
    if override is not None:
        return override == "1" and len(argv) == 1
    return executable.name == APP_EXECUTABLE_NAME and list(argv) == [""]


def configure_bundled_runtime(executable: pathlib.Path) -> bool:
    app_path, package_path = bundled_resource_paths(executable)
    if not app_path.is_dir() or not package_path.is_dir():
        return False

    user_site = pathlib.Path(site.getusersitepackages()).resolve()
    sys.path[:] = [
        entry
        for entry in sys.path
        if pathlib.Path(entry or os.getcwd()).resolve() != user_site
    ]
    sys.path[0:0] = [str(app_path), str(package_path)]
    sys.dont_write_bytecode = True
    return True


executable_path = pathlib.Path(sys.executable)
is_bundled = configure_bundled_runtime(executable_path)
if is_bundled and should_launch_bundled_app(executable_path, sys.argv):
    from menu_app import main

    main()
    raise SystemExit(0)
