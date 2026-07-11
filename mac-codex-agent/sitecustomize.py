"""Launch the menu application when the bundled Python executable starts."""

import os
import sys


if os.environ.get("STOPWATCH_CODEX_APP") == "1" and len(sys.argv) == 1:
    from menu_app import main

    main()
    raise SystemExit(0)
