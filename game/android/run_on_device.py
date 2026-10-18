#!/usr/bin/env python3
"""Builds the Android player, installs it on a device, starts it and follows its log.

Steps, each of which can be skipped:
  1. the native library: the android-arm64 CMake preset (libmain.so);
  2. the APK: Gradle's assembleDebug in game/android, with a deploy folder's data as its assets
     (--content);
  3. install and start on the device, then print the app's logcat lines until it exits, the
     time limit passes or Ctrl+C. A native crash is symbolized with ndk-stack against the
     unstripped libmain.so.

The Android SDK comes from ANDROID_HOME (or ANDROID_SDK_ROOT, or the default install folder) and
Java from JAVA_HOME (or the JDK bundled with Android Studio).

Examples:
  python game/android/run_on_device.py --content ../Deploy2048
  python game/android/run_on_device.py --skip-native --seconds 20
  python game/android/run_on_device.py --device 192.168.0.158:34119
"""

import argparse
import glob
import os
import platform
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

PACKAGE_NAME = "com.unravel.player"
ACTIVITY_NAME = "com.unravel.player/.UnravelActivity"
CMAKE_PRESET = "android-arm64"
ANDROID_DIR = Path(__file__).resolve().parent
REPO_ROOT = ANDROID_DIR.parent.parent
NATIVE_LIBRARY_DIR = REPO_ROOT / "develop" / "build" / CMAKE_PRESET / "bin"
APK_PATH = ANDROID_DIR / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
LOG_DIR = ANDROID_DIR / "build" / "device-logs"
IS_WINDOWS = platform.system() == "Windows"
EXECUTABLE_SUFFIX = ".exe" if IS_WINDOWS else ""
# Tags whose lines are printed even when they come from another process: the crash reporter.
CRASH_TAGS = ("DEBUG", "crash_dump64", "tombstoned")
LOGCAT_LINE = re.compile(r"^\S+\s+\S+\s+(\d+)\s+\d+\s+([VDIWEF])\s+(.*?)\s*:")
PROCESS_START = re.compile(r"Start proc (\d+):" + re.escape(PACKAGE_NAME) + r"/")


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def find_android_home() -> Path:
    candidates = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    if IS_WINDOWS:
        candidates.append(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"))
    elif platform.system() == "Darwin":
        candidates.append(os.path.expanduser("~/Library/Android/sdk"))
    else:
        candidates.append(os.path.expanduser("~/Android/Sdk"))
    for candidate in candidates:
        if candidate and (Path(candidate) / "platform-tools").is_dir():
            return Path(candidate)
    fail("no Android SDK found. Set ANDROID_HOME to the SDK folder.")


def find_java_home() -> Path:
    patterns = []
    if os.environ.get("JAVA_HOME"):
        patterns.append(os.environ["JAVA_HOME"])
    if IS_WINDOWS:
        patterns.append("C:/Program Files/Android/Android Studio*/jbr")
    elif platform.system() == "Darwin":
        patterns.append("/Applications/Android Studio*.app/Contents/jbr/Contents/Home")
    else:
        patterns += ["/opt/android-studio*/jbr", os.path.expanduser("~/android-studio*/jbr"),
                     "/snap/android-studio/current/jbr"]
    for pattern in patterns:
        for candidate in sorted(glob.glob(pattern), reverse=True):
            if (Path(candidate) / "bin" / f"java{EXECUTABLE_SUFFIX}").is_file():
                return Path(candidate)
    fail("no JDK found. Set JAVA_HOME, or install Android Studio, which bundles one.")


def find_ndk(android_home: Path) -> Path:
    gradle_file = (ANDROID_DIR / "app" / "build.gradle.kts").read_text()
    match = re.search(r'ndkVersion\s*=\s*"([^"]+)"', gradle_file)
    ndk = android_home / "ndk" / (match.group(1) if match else "")
    if not match or not ndk.is_dir():
        fail(f"the NDK named in app/build.gradle.kts is not installed under {android_home / 'ndk'}.")
    return ndk


def run(command: list, cwd: Path, environment: dict) -> None:
    print("> " + " ".join(str(part) for part in command), flush=True)
    if subprocess.run([str(part) for part in command], cwd=cwd, env=environment).returncode != 0:
        fail(f"{Path(str(command[0])).name} failed.")


def build_native(environment: dict, configure: bool) -> None:
    if configure or not (REPO_ROOT / "develop" / "build" / CMAKE_PRESET / "CMakeCache.txt").is_file():
        run(["cmake", "--preset", CMAKE_PRESET], REPO_ROOT, environment)
    run(["cmake", "--build", "--preset", CMAKE_PRESET], REPO_ROOT, environment)


def build_apk(environment: dict, content: str) -> None:
    gradle = ANDROID_DIR / ("gradlew.bat" if IS_WINDOWS else "gradlew")
    command = [gradle, "assembleDebug", "--console=plain"]
    if content:
        if not (Path(content) / "data").is_dir():
            fail(f"{content} is not a deploy folder (it has no data folder).")
        command.append(f"-Punravel.content={Path(content).resolve()}")
    run(command, ANDROID_DIR, environment)


def adb_output(adb: list, *arguments: str) -> str:
    return subprocess.run(adb + list(arguments), capture_output=True, text=True).stdout


def select_device(adb_path: Path, requested: str) -> list:
    """The adb command line for one device: the requested one, the only one attached, or the first
    device found over wireless debugging."""
    if requested:
        if ":" in requested:
            adb_output([str(adb_path)], "connect", requested)
        return [str(adb_path), "-s", requested]
    devices = [line.split()[0] for line in adb_output([str(adb_path)], "devices").splitlines()[1:]
               if line.strip().endswith("device")]
    if not devices:
        services = re.findall(r"_adb-tls-connect\._tcp\s+(\S+)", adb_output([str(adb_path)], "mdns", "services"))
        for address in services:
            if "connected" in adb_output([str(adb_path)], "connect", address):
                devices = [address]
                break
    if not devices:
        fail("no device. Connect one over USB, or turn on wireless debugging after pairing it once.")
    if len(devices) > 1:
        fail("more than one device: " + ", ".join(devices) + ". Pick one with --device.")
    print(f"device: {devices[0]}")
    return [str(adb_path), "-s", devices[0]]


def read_lines(process: subprocess.Popen, lines: queue.Queue) -> None:
    for line in process.stdout:
        lines.put(line.rstrip("\n"))
    lines.put(None)


def follow_log(adb: list, seconds: float, log_file: Path) -> list:
    """Prints the app's logcat lines, and the crash reporter's, until the app's process ends or the
    time limit passes. Returns every printed line."""
    logcat = subprocess.Popen(adb + ["logcat", "-v", "threadtime"], stdout=subprocess.PIPE, text=True,
                              encoding="utf-8", errors="replace")
    lines: queue.Queue = queue.Queue()
    threading.Thread(target=read_lines, args=(logcat, lines), daemon=True).start()
    deadline = time.monotonic() + seconds if seconds > 0 else None
    app_pid = None
    printed = []
    try:
        while deadline is None or time.monotonic() < deadline:
            try:
                line = lines.get(timeout=0.5)
            except queue.Empty:
                if app_pid and not adb_output(adb, "shell", "pidof", PACKAGE_NAME).strip():
                    time.sleep(1.0)
                    drain(lines, app_pid, printed)
                    print(f"--- {PACKAGE_NAME} (pid {app_pid}) has exited")
                    break
                continue
            if line is None:
                break
            start = PROCESS_START.search(line)
            if start:
                app_pid = start.group(1)
            if is_app_line(line, app_pid):
                print(line, flush=True)
                printed.append(line)
    except KeyboardInterrupt:
        pass
    finally:
        logcat.terminate()
    log_file.write_text("\n".join(printed) + "\n", encoding="utf-8")
    return printed


def drain(lines: queue.Queue, app_pid: str, printed: list) -> None:
    while not lines.empty():
        line = lines.get()
        if line is not None and is_app_line(line, app_pid):
            print(line, flush=True)
            printed.append(line)


def is_app_line(line: str, app_pid: str) -> bool:
    match = LOGCAT_LINE.match(line)
    if not match:
        return False
    return match.group(1) == app_pid or match.group(3) in CRASH_TAGS


def symbolize_crash(ndk: Path, log_file: Path, printed: list) -> None:
    if not any(" F DEBUG " in line for line in printed):
        return
    ndk_stack = ndk / ("ndk-stack.cmd" if IS_WINDOWS else "ndk-stack")
    result = subprocess.run([str(ndk_stack), "-sym", str(NATIVE_LIBRARY_DIR), "-i", str(log_file)],
                            capture_output=True, text=True)
    symbolized = log_file.with_name(log_file.stem + "-symbolized.txt")
    symbolized.write_text(result.stdout, encoding="utf-8")
    print("\n--- crash, symbolized:")
    print(result.stdout)
    print(f"--- saved to {symbolized}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", default="", help="adb serial or ip:port of the device to use")
    parser.add_argument("--content", default="",
                        help="deploy folder whose data the APK carries (default: none, the player has no game)")
    parser.add_argument("--configure", action="store_true", help="re-run the CMake configure step")
    parser.add_argument("--skip-native", action="store_true", help="use the libmain.so already built")
    parser.add_argument("--skip-apk", action="store_true", help="use the APK already built")
    parser.add_argument("--no-install", action="store_true", help="only build")
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="stop following the log after this many seconds (default: until the app exits)")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    android_home = find_android_home()
    environment = dict(os.environ, ANDROID_HOME=str(android_home), JAVA_HOME=str(find_java_home()))
    ndk = find_ndk(android_home)
    if not arguments.skip_native:
        build_native(environment, arguments.configure)
    if not arguments.skip_apk:
        build_apk(environment, arguments.content)
    if arguments.no_install:
        return
    if not APK_PATH.is_file():
        fail(f"no APK at {APK_PATH}.")
    adb = select_device(android_home / "platform-tools" / f"adb{EXECUTABLE_SUFFIX}", arguments.device)
    run(adb + ["install", "-r", APK_PATH], ANDROID_DIR, environment)
    run(adb + ["shell", "am", "force-stop", PACKAGE_NAME], ANDROID_DIR, environment)
    run(adb + ["logcat", "-c"], ANDROID_DIR, environment)
    run(adb + ["shell", "am", "start", "-W", "-n", ACTIVITY_NAME], ANDROID_DIR, environment)
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_file = LOG_DIR / time.strftime("run-%Y%m%d-%H%M%S.txt")
    printed = follow_log(adb, arguments.seconds, log_file)
    print(f"--- log saved to {log_file}")
    symbolize_crash(ndk, log_file, printed)


if __name__ == "__main__":
    main()
