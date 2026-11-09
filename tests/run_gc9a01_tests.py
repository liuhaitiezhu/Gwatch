"""Build the actual GC9A01 driver against a host HAL and check the SPI stream."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get("HOST_CC") or shutil.which("gcc")
if not compiler:
    raise SystemExit("Host GCC is required; set HOST_CC to its executable path.")

with tempfile.TemporaryDirectory(prefix="gwatch-gc9a01-tests-") as temporary:
    for software, fixture in [(0, "test_gc9a01.c"), (1, "test_gc9a01_software.c")]:
        name = f"gc9a01_tests_{software}" + (".exe" if os.name == "nt" else "")
        executable = Path(temporary) / name
        subprocess.run([
            compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
            f"-DGC9A01_SOFTWARE_SPI={software}",
            "-I", str(root / "tests/fakes"), "-I", str(root / "Core/Inc"),
            str(root / "Core/Src/bsp_gc9a01.c"),
            str(root / "tests" / fixture), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
