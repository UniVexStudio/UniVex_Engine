#!/usr/bin/env python3
"""Build the native runtime and a locally debug-signed Android APK without Gradle."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zipfile


def run(*command):
    subprocess.run([str(part) for part in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--dependency-source-cache', type=Path)
    parser.add_argument('--native-build-dir', type=Path)
    parser.add_argument('--cooked-project', type=Path, help='Cooked .uvproject file for native scene launch')
    parser.add_argument('--abi', choices=['arm64-v8a', 'x86_64'], default='arm64-v8a')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    sdk = args.sdk.resolve()
    build = args.build_dir.resolve()
    ndk = sdk / 'ndk/29.0.14206865'
    tools = sdk / 'build-tools/35.0.0'
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    for required in [ndk / 'build/cmake/android.toolchain.cmake', tools / 'aapt2', sdk / 'platforms/android-35/android.jar']:
        if not required.is_file():
            parser.error(f'Missing Android tool: {required}')
    build.mkdir(parents=True, exist_ok=True)
    native = args.native_build_dir.resolve() if args.native_build_dir else build / 'native'
    cached_sources = []
    if args.dependency_source_cache:
        for source in args.dependency_source_cache.resolve().glob('*-src'):
            cached_sources.append(f'-DFETCHCONTENT_SOURCE_DIR_{source.name[:-4].upper()}={source}')
    run('cmake', '-S', root, '-B', native, '-G', 'Ninja',
        f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake',
        f'-DANDROID_ABI={args.abi}', '-DANDROID_PLATFORM=android-28',
        '-DUVE_BUILD_TESTS=OFF', '-DCMAKE_BUILD_TYPE=Release', *cached_sources)
    run('cmake', '--build', native, '--target', 'univex_android', '--parallel', args.jobs)
    assets = build / 'assets'
    shutil.copytree(root / 'Engine/Runtime/RHI/Shader/built_in', assets / 'shaders', dirs_exist_ok=True)
    # Rebuild the staging assets to avoid shipping a previous project's stale content.
    project_assets = assets / 'project'
    if project_assets.exists():
        shutil.rmtree(project_assets)
    index = assets / 'project-files.txt'
    if index.exists():
        index.unlink()
    if args.cooked_project:
        manifest = args.cooked_project.resolve()
        source_root = manifest.parent
        if not manifest.is_file() or not (source_root / 'cooked.json').is_file():
            parser.error('--cooked-project must name a cooked project manifest')
        metadata = json.loads((source_root / 'cooked.json').read_text(encoding='utf-8'))
        names = metadata.get('runtimeFiles', [])
        if not names or len(names) != len(set(names)) or len(names) > 100000:
            parser.error('Missing or invalid cooked runtime file index; recook the project')
        files = []
        for name in sorted(names):
            relative = Path(name)
            if relative.is_absolute() or not relative.parts or any(part in {'.', '..'} for part in relative.parts):
                parser.error('Invalid cooked runtime file path')
            path = source_root / relative
            if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(source_root):
                parser.error('Missing or escaping cooked runtime file')
            files.append(path)
        total = 0
        lines = []
        for path in files:
            if path.is_symlink() or path.resolve().parent != path.parent.resolve():
                parser.error('Cooked APK content must not contain symlinks')
            relative = path.relative_to(source_root).as_posix()
            if '\n' in relative or '\r' in relative or '..' in Path(relative).parts:
                parser.error('Invalid cooked asset path')
            size = path.stat().st_size
            total += size
            if size > 512 * 1024 * 1024 or total > 1024 * 1024 * 1024 or len(files) > 100000:
                parser.error('Cooked APK content exceeds extraction bounds')
            # Native runtime binaries are built separately, never copied as content.
            destination = project_assets / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
            lines.append(relative)
        index.write_text(manifest.name + '\n' + '\n'.join(lines) + '\n', encoding='utf-8')
    unsigned = build / 'unsigned.apk'
    run(tools / 'aapt2', 'link', '-I', sdk / 'platforms/android-35/android.jar',
        '--manifest', root / 'Engine/App/Android/AndroidManifest.xml', '-A', assets, '-o', unsigned)
    library = native / 'Engine/App/Android/libunivex_android.so'
    stripped = build / 'libunivex_android.so'
    shutil.copy2(library, stripped)
    run(ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip', '--strip-unneeded', stripped)
    with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_STORED) as package:
        package.write(stripped, f'lib/{args.abi}/libunivex_android.so')
        # Native C++ uses the shared libc++ runtime by default.
        runtime_abi = 'aarch64-linux-android' if args.abi == 'arm64-v8a' else 'x86_64-linux-android'
        runtime = ndk / f'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/{runtime_abi}/libc++_shared.so'
        package.write(runtime, f'lib/{args.abi}/libc++_shared.so')
        # aapt2 omits dotfiles such as the stable .uvassetdb catalog. Add every omitted
        # staged asset before alignment/signing, then verify the native extraction index.
        existing = set(package.namelist())
        for asset in sorted(assets.rglob('*')):
            if asset.is_file():
                archive_name = 'assets/' + asset.relative_to(assets).as_posix()
                if archive_name not in existing:
                    package.write(asset, archive_name)
                    existing.add(archive_name)
        if args.cooked_project:
            for name in lines:
                if 'assets/project/' + name not in existing:
                    raise RuntimeError('Cooked asset missing from APK: ' + name)
    aligned = build / 'aligned.apk'
    run(tools / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned)
    key = build / 'debug.keystore'
    if not key.exists():
        run('keytool', '-genkeypair', '-keystore', key, '-storepass', 'android', '-keypass', 'android',
            '-alias', 'androiddebugkey', '-dname', 'CN=Android Debug,O=UniVex,C=US', '-keyalg', 'RSA',
            '-keysize', '2048', '-validity', '365')
    output = build / f'univex-runtime-{args.abi}.apk'
    run(tools / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
        '--out', output, aligned)
    run(tools / 'apksigner', 'verify', '--verbose', output)
    run(tools / 'zipalign', '-c', '-P', '16', '4', output)
    print(f'APK: {output}')


if __name__ == '__main__':
    main()
