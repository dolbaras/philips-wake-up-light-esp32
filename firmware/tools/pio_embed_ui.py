# PlatformIO extra_script: перед сборкой встраиваем свежую веб-панель (ui/index.html → src/net/ui_gz.h).
import os, subprocess, sys
Import("env")  # noqa: F821  (определено PlatformIO)
script = os.path.join(env["PROJECT_DIR"], "tools", "embed_ui.py")  # noqa: F821
subprocess.check_call([sys.executable, script])
