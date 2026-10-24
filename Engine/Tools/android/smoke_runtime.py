#!/usr/bin/env python3
"""Exercise the native runtime on an already booted Android test device/emulator."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--screenshot', type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be positive')
    args.report.unlink(missing_ok=True)
    report = {'passed': False, 'checks': [], 'device': args.serial,
              'performance_verified': False}

    def adb(*command, timeout=30):
        return subprocess.run([args.adb, '-s', args.serial, *command], check=True,
                              capture_output=True, timeout=timeout).stdout.decode(errors='replace')

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

    try:
        if adb('shell', 'getprop', 'sys.boot_completed').strip() != '1':
            report['blocked'] = 'Android boot has not completed'
            return 2
        adb('install', '--no-streaming', '-r', str(args.apk.resolve()), timeout=60)
        adb('shell', 'am', 'force-stop', 'com.univex.runtime')
        # Fresh test log for this dedicated device session.
        adb('logcat', '-c')
        adb('shell', 'am', 'start', '-n', 'com.univex.runtime/android.app.NativeActivity')
        wait_for('UVE_ANDROID_INITIALIZED')
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
        text = wait_for('UVE_ANDROID_SURFACE_ATTACHED')
        if text.count('UVE_ANDROID_INITIALIZED') != 1:
            raise RuntimeError('Engine was recreated instead of retaining its scene')
        if args.screenshot:
            shot = subprocess.run([args.adb, '-s', args.serial, 'exec-out', 'screencap', '-p'],
                                  capture_output=True, check=True, timeout=30).stdout
            if not shot.startswith(b'\x89PNG\r\n\x1a\n'):
                raise RuntimeError('Device screenshot is not a PNG')
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            args.screenshot.write_bytes(shot)
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
