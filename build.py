"""
Build script: app.py -> dieuchinh.exe
Usage: python3 build.py
"""

import subprocess
import sys
import os

PYTHON = sys.executable
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(PROJECT_DIR, "app.py")
ICON_PNG = os.path.join(PROJECT_DIR, "adjustments.png")
ICON_ICO = os.path.join(PROJECT_DIR, "adjustments.ico")
OUTPUT_NAME = "dieuchinh"


def install_package(package):
    print(f"[BUILD] Installing {package}...")
    subprocess.check_call([PYTHON, "-m", "pip", "install", package, "--quiet"])


def ensure_deps():
    for pkg in ("pillow", "pyinstaller"):
        try:
            __import__("PIL" if pkg == "pillow" else pkg)
        except ImportError:
            install_package(pkg)


def convert_png_to_ico():
    from PIL import Image

    if not os.path.exists(ICON_PNG):
        print(f"[WARN] Icon not found: {ICON_PNG} — building without icon")
        return None

    img = Image.open(ICON_PNG).convert("RGBA")
    img.save(
        ICON_ICO,
        format="ICO",
        sizes=[(16, 16), (32, 32), (48, 48), (256, 256)],
    )
    print(f"[BUILD] Icon converted: {ICON_ICO}")
    return ICON_ICO


def run_pyinstaller(icon_path):
    cmd = [
        PYTHON, "-m", "PyInstaller",
        "--onefile",
        "--windowed",
        "--name", OUTPUT_NAME,
        "--distpath", os.path.join(PROJECT_DIR, "dist"),
        "--workpath", os.path.join(PROJECT_DIR, "build"),
        "--specpath", PROJECT_DIR,
    ]

    if icon_path and os.path.exists(icon_path):
        cmd += ["--icon", icon_path]

    # Bundle PNG so resource_path() works in the exe
    if os.path.exists(ICON_PNG):
        cmd += ["--add-data", f"{ICON_PNG};."]

    cmd.append(SOURCE)

    print("[BUILD] Running PyInstaller...")
    print(" ".join(cmd))
    subprocess.check_call(cmd, cwd=PROJECT_DIR)


def main():
    print("=" * 50)
    print(f"  Building {OUTPUT_NAME}.exe from app.py")
    print("=" * 50)

    print("[BUILD] Checking dependencies...")
    ensure_deps()

    icon_path = convert_png_to_ico()
    run_pyinstaller(icon_path)

    exe_path = os.path.join(PROJECT_DIR, "dist", f"{OUTPUT_NAME}.exe")
    if os.path.exists(exe_path):
        size_mb = os.path.getsize(exe_path) / (1024 * 1024)
        print(f"\n[OK] Build successful!")
        print(f"     Output: {exe_path}")
        print(f"     Size  : {size_mb:.1f} MB")
    else:
        print("\n[FAIL] exe not found — check PyInstaller output above")
        sys.exit(1)


if __name__ == "__main__":
    main()
