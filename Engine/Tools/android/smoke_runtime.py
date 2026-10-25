#!/usr/bin/env python3
"""Exercise the native runtime on an already booted Android test device/emulator."""
import argparse
import json
import math
import os
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
    parser.add_argument('--label', default='', help='Name for annotations, e.g. demo or cooked')
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be positive')
    args.report.unlink(missing_ok=True)
    report = {'passed': False, 'checks': [], 'device': args.serial,
              'performance_verified': False}
    label = args.label or args.apk.name

    def annotate(level, message):
        """Make the failure readable where somebody is already looking.

        The job log is the long version, but a red check with "Process completed with exit code 1"
        and nothing else is how a black screen stays unexplained through three CI runs. The reason,
        whether the app was even alive, and the end of the device log go into the annotation.
        """
        if not os.environ.get('GITHUB_ACTIONS'):
            return
        # Annotations are one line; GitHub escapes newlines but a wall of them is unreadable.
        text = ' '.join(str(message).split())
        if len(text) > 3500:
            text = text[:3500] + ' ...'
        print(f'::{level} title={label} apk smoke::{text}', flush=True)

    def process_is_running():
        try:
            return bool(adb('shell', 'pidof', 'com.univex.runtime').strip())
        except (subprocess.SubprocessError, OSError):
            return False

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
            # A rejected cooked project still starts - the runtime falls back to its own scene - so
            # waiting for the loaded marker would sit here until the timeout with the answer already
            # on the device. Fail on it now, with the reason the runtime gave.
            if 'UVE_ANDROID_COOKED_PROJECT_REJECTED' in text:
                reason = next(line for line in text.splitlines()
                              if 'UVE_ANDROID_COOKED_PROJECT_REJECTED' in line)
                raise RuntimeError('The runtime rejected the cooked project: ' + reason.strip())
            # Same reasoning for a stopped runtime: waiting out the timeout for a marker that
            # cannot arrive turns "startup failed, here is why" into "something is missing".
            if marker != 'UVE_ANDROID_FAILED' and 'UVE_ANDROID_FAILED' in text:
                reason = next(line for line in text.splitlines() if 'UVE_ANDROID_FAILED' in line)
                raise RuntimeError('The runtime stopped: ' + reason.strip())
            if text.count(marker) >= minimum:
                print(marker, flush=True)
                report['checks'].append(marker)
                return text
            time.sleep(1)
        raise RuntimeError(f'Missing native evidence: {marker}')

    def sample_screen():
        """Return (width, height, pixel reader) for a fresh device screenshot, or None."""
        raw = subprocess.run([args.adb, '-s', args.serial, 'exec-out', 'screencap'],
                             capture_output=True, check=True, timeout=30).stdout
        if len(raw) < 12:
            return None
        width, height, pixel_format = struct.unpack_from('<III', raw)
        header = len(raw) - width * height * 4
        if width <= 0 or height <= 0 or pixel_format not in (1, 2) or header not in (12, 16):
            return None

        def pixel(x, y):
            offset = header + (y * width + x) * 4
            return raw[offset], raw[offset + 1], raw[offset + 2]

        return width, height, pixel

    def screen_samples():
        """Read the device framebuffer.

        Returns (luminance of a 10x10 sample grid, centre pixel) or None when the screencap cannot be
        parsed. Format 1 is RGBA_8888 and 2 is RGBX_8888; anything else, or a header that does not
        match the payload size, means this device's screencap is not something we can read.
        """
        raw = subprocess.run([args.adb, '-s', args.serial, 'exec-out', 'screencap'],
                             capture_output=True, check=True, timeout=30).stdout
        if len(raw) < 12:
            return None
        width, height, pixel_format = struct.unpack_from('<III', raw)
        header = len(raw) - width * height * 4
        if width <= 0 or height <= 0 or pixel_format not in (1, 2) or header not in (12, 16):
            return None

        def sample(x, y):
            offset = header + (y * width + x) * 4
            return raw[offset], raw[offset + 1], raw[offset + 2]

        grid = []
        for row in range(1, 11):
            for column in range(1, 11):
                red, green, blue = sample(width * column // 11, height * row // 11)
                grid.append((red * 299 + green * 587 + blue * 114) // 1000)
        return grid, sample(width // 2, height // 2)

    def wait_for_screen_with_content():
        """Fail when the device is showing an empty frame.

        This is the shape of the report that started all of it: every lifecycle marker present, the
        engine ticking, and the phone showing nothing. Brightness alone cannot detect that, because a
        frame containing only the scene's clear colour is uniformly dark-but-not-black and would pass
        any threshold. Contrast can: a frame with geometry in it has pixels that differ from the
        background, an empty one does not. So the check asks for range, not for brightness.

        The cooked sample has one more property worth asserting - its cube is blue - so a cooked run
        also requires the centre pixel to stay blue-dominant. That is the check the sample exists
        for, and it is skipped for the demo, whose scene is a neutral default cube.
        """
        deadline = time.monotonic() + min(args.timeout, 60)
        darkest, brightest = 255, 0
        readable = 0
        while time.monotonic() < deadline:
            frame = screen_samples()
            if frame is not None:
                readable += 1
                grid, centre = frame
                darkest, brightest = min(grid), max(grid)
                # 32/255 of range is roughly 12% - far more than dithering or a gradient in the
                # clear colour can produce, and far less than a lit cube against the background.
                if brightest >= 24 and brightest - darkest >= 32:
                    red, green, blue = centre
                    if args.expect_cooked_project:
                        if not (blue > green + 10 and green > red + 10 and blue > 80):
                            time.sleep(1)
                            continue
                        report['checks'].append('UVE_ANDROID_VISIBLE_PIXELS')
                    report['checks'].append('UVE_ANDROID_SCREEN_WITH_CONTENT')
                    print(f'UVE_ANDROID_SCREEN_WITH_CONTENT range={brightest - darkest}', flush=True)
                    return
            time.sleep(1)
        if not readable:
            # A screencap that never parsed is a broken check, not a black screen; saying so is the
            # difference between fixing the emulator and chasing a rendering bug that does not exist.
            raise RuntimeError('Device screencaps could not be read; cannot judge what is on screen')
        raise RuntimeError('The device screen has no content: '
                           f'luminance {darkest}..{brightest} over a 10x10 sample grid')

    def assert_no_failure_marker():
        text = logs()
        if 'UVE_ANDROID_FAILED' in text:
            reason = next(line for line in text.splitlines() if 'UVE_ANDROID_FAILED' in line)
            raise RuntimeError('The runtime reported a startup failure: ' + reason.strip())
        report['checks'].append('UVE_ANDROID_NO_FAILURE')

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
        adb('shell', 'am', 'start', '-n', 'com.univex.runtime/android.app.NativeActivity')
        wait_for('UVE_ANDROID_RESUMED', 2)
        text = wait_for('UVE_ANDROID_RENDERED', 2)
        if text.count('UVE_ANDROID_INITIALIZED') != 1:
            raise RuntimeError('Engine was recreated instead of retaining its scene')
        # Android does not have to destroy the window when the app goes home. On a device that keeps
        # it, the surface was never detached, so "wait for the reattach" is waiting for an event the
        # platform is entitled not to send - which is exactly how this smoke failed once while the
        # runtime was doing nothing wrong. What has to be true is conditional: if the surface went
        # away it must come back, and either way the resumed scene has to draw again (the wait above
        # is what proves the drawing).
        if 'UVE_ANDROID_SURFACE_DETACHED' in text:
            if 'UVE_ANDROID_SURFACE_ATTACHED' not in text:
                raise RuntimeError('The surface was detached on pause and never reattached')
            report['checks'].append('UVE_ANDROID_SURFACE_ATTACHED')
        else:
            report['checks'].append('UVE_ANDROID_SURFACE_KEPT')
        assert_no_failure_marker()
        wait_for_screen_with_content()
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
        annotate('notice', f'passed {len(report["checks"])} checks, screen lit')
        return 0
    except (subprocess.SubprocessError, OSError, RuntimeError) as error:
        report['error'] = str(error)
        running = process_is_running()
        report['process_running'] = running
        try:
            report['native_log_tail'] = logs()[-6000:]
            print(report['native_log_tail'], flush=True)
        except (subprocess.SubprocessError, OSError):
            report['native_log_tail'] = ''
        annotate('error', f'{error} | app running: {running} | log tail: '
                          f'{report["native_log_tail"][-1200:]}')
        return 1
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
