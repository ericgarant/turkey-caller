# Fails the build if firmware.bin is larger than the Phase 0 budget (2.25 MB, 90% of the 2.5 MB app partition).
import os
Import("env")  # noqa: F821  (provided by PlatformIO)

LIMIT = int(2.25 * 1024 * 1024)


def check_size(source, target, env):
    path = target[0].get_abspath()
    size = os.path.getsize(path)
    pct = 100.0 * size / LIMIT
    print(f"App binary: {size} bytes ({size / 1048576:.2f} MB), {pct:.0f}% of the 2.25 MB budget")
    if size > LIMIT:
        print("ERROR: app binary is over the 2.25 MB budget")
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)  # noqa: F821
