# PlatformIO post-build script: merges bootloader + partitions + app into ONE image
# (web/firmware/cyd-chess-merged.bin) that the browser flasher writes at offset 0.
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
import os


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    platform = env.PioPlatform()
    framework = platform.get_package_dir("framework-arduinoespressif32")
    esptool = os.path.join(platform.get_package_dir("tool-esptoolpy"), "esptool.py")
    boot_app0 = os.path.join(framework, "tools", "partitions", "boot_app0.bin")
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "web", "firmware")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    out = os.path.join(out_dir, "cyd-chess-merged.bin")
    cmd = ('"{py}" "{esptool}" --chip esp32 merge_bin -o "{out}" '
           '--flash_mode dio --flash_freq 40m --flash_size 4MB '
           '0x1000 "{b}/bootloader.bin" 0x8000 "{b}/partitions.bin" '
           '0xe000 "{app0}" 0x10000 "{b}/firmware.bin"').format(
        py=env.subst("$PYTHONEXE"), esptool=esptool, out=out, b=build, app0=boot_app0)
    print("Merging flash image -> " + out)
    env.Execute(cmd)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
