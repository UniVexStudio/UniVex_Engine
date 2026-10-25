#!/usr/bin/env python3
"""Exercise the native runtime on an already booted Android test device/emulator."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--screenshot', type=Path)
    parser.add_argument('--expect-cooked-project', action='store_true')
    parser.add_argument('--memory-report', type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be positive')
    args.report.unlink(missing_ok=True)
    report = {'passed': False, 'checks': [], 'device': args.serial,
              'performance_verified': False}

    def adb(*command, timeout=None):
        return subprocess.run([args.adb, '-s', args.serial, *command], check=True,
                              capture_output=True,
                              timeout=args.timeout if timeout is None else timeout).stdout.decode(errors='replace')

    def logs():
        return adb('logcat', '-d', '-s', 'UniVex:I', '*:S')

    def wait_for(marker, minimum=1):
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            text = logs()
            if text.count(marker) >= minimum:
                print(marker, flush=True)
                report['checks'].append(marker)
                return text
            time.sleep(1)
        raise RuntimeError(f'Missing native evidence: {marker}')

    def wait_for_visible_scene():
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            raw = subprocess.run([args.adb, '-s', args.serial, 'exec-out', 'screencap'],
                                 capture_output=True, check=True, timeout=30).stdout
            if len(raw) >= 12:
                width, height, pixel_format = struct.unpack_from('<III', raw)
                header = len(raw) - width * height * 4
                if width > 0 and height > 0 and pixel_format in (1, 2) and header in (12, 16):
                    offset = header + ((height // 2) * width + width // 2) * 4
                    red, green, blue = raw[offset:offset + 3]
                    if blue > green + 10 and green > red + 10 and blue > 80:
                        report['checks'].append('UVE_ANDROID_VISIBLE_PIXELS')
                        print('UVE_ANDROID_VISIBLE_PIXELS', flush=True)
                        return
            time.sleep(1)
        raise RuntimeError('The resumed cube is not visible at the screen center')

    try:
        if adb('shell', 'getprop', 'sys.boot_completed').strip() != '1':
            report['blocked'] = 'Android boot has not completed'
            return 2
        adb('install', '--no-incremental', '-r', str(args.apk.resolve()), timeout=max(60, args.timeout))
        adb('shell', 'am', 'force-stop', 'com.univex.runtime')
        # Fresh test log for this dedicated device session.
        adb('logcat', '-c')
        adb('shell', 'am', 'start', '-n', 'com.univex.runtime/android.app.NativeActivity')
        wait_for('UVE_ANDROID_INITIALIZED')
        if args.expect_cooked_project:
            wait_for('UVE_ANDROID_COOKED_PROJECT_LOADED')
        wait_for('UVE_ANDROID_RENDERED')
        adb('shell', 'input', 'swipe', '160', '300', '160', '300', '1500')
        wait_for('UVE_ANDROID_TOUCH action=0')
        wait_for('UVE_ANDROID_TOUCH action=1')
        wait_for('UVE_ANDROID_TOUCH_APPLIED active=1')
        wait_for('UVE_ANDROID_TOUCH_APPLIED active=0')
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        wait_for('UVE_ANDROID_PAUSED')
        wait_for('UVE_ANDROID_SURFACE_DETACHED')
        adb('shell', 'am', 'start', '-n', 'com.univex.runtime/android.app.NativeActivity')
        wait_for('UVE_ANDROID_RESUMED', 2)
        wait_for('UVE_ANDROID_SURFACE_ATTACHED')
        text = wait_for('UVE_ANDROID_RENDERED', 2)
        if text.count('UVE_ANDROID_INITIALIZED') != 1:
            raise RuntimeError('Engine was recreated instead of retaining its scene')
        wait_for_visible_scene()
        if args.memory_report:
            memory = adb('shell', 'dumpsys', 'meminfo', 'com.univex.runtime')
            if 'TOTAL' not in memory:
                raise RuntimeError('Native process memory report unavailable')
            args.memory_report.parent.mkdir(parents=True, exist_ok=True)
            args.memory_report.write_text(memory, encoding='utf-8')
            report['memory_measurement'] = 'Android dumpsys meminfo; emulator/device as supplied'
        if args.screenshot:
            shot = subprocess.run([args.adb, '-s', args.serial, 'exec-out', 'screencap', '-p'],
                                  capture_output=True, check=True, timeout=30).stdout
            if not shot.startswith(b'\x89PNG\r\n\x1a\n'):
                raise RuntimeError('Device screenshot is not a PNG')
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            args.screenshot.write_bytes(shot)
        if args.expect_cooked_project:
            # The extracted content is replaced on process startup; application data must survive.
            marker = 'files/user-data/cooked-smoke-marker'
            adb('shell', 'run-as', 'com.univex.runtime', 'touch', marker)
            adb('shell', 'am', 'force-stop', 'com.univex.runtime')
            adb('shell', 'am', 'start', '-n', 'com.univex.runtime/android.app.NativeActivity')
            wait_for('UVE_ANDROID_COOKED_PROJECT_LOADED', 2)
            wait_for('UVE_ANDROID_RENDERED', 3)
            adb('shell', 'run-as', 'com.univex.runtime', 'cat', marker)
            report['checks'].append('UVE_ANDROID_PERSISTENT_DATA_PRESERVED')
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK')
        wait_for('UVE_ANDROID_DESTROYED')
        report['passed'] = True
        return 0
    except (subprocess.SubprocessError, OSError, RuntimeError) as error:
        report['error'] = str(error)
        try:
            report['native_log_tail'] = logs()[-6000:]
            print(report['native_log_tail'], flush=True)
        except (subprocess.SubprocessError, OSError):
            pass
        return 1
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
