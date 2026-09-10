#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import tarfile
from pathlib import Path


PRODUCT = "teez"
RUNTIME_PATTERNS = {
    "linux": ["*.so", "*.so.*"],
    "macos": ["*.dylib"],
}


def normalized_version(raw: str) -> str:
    return raw[1:] if raw.startswith("v") else raw


def copy_runtime_files(binary_path: Path, stage_bin_dir: Path, platform: str) -> None:
    for pattern in RUNTIME_PATTERNS.get(platform, []):
        for runtime_path in binary_path.parent.glob(pattern):
            if runtime_path.is_file():
                shutil.copy2(runtime_path, stage_bin_dir / runtime_path.name)


def copy_bundled_plugins(plugins_dir: Path, stage_root: Path) -> None:
    target_dir = stage_root / "share" / "teez" / "plugins"
    if target_dir.exists():
        shutil.rmtree(target_dir)
    shutil.copytree(plugins_dir, target_dir)


def bundle_linux_libraries(binary_path: Path, lib_dir: Path, repo_root: Path) -> None:
    script = repo_root / "scripts" / "ci" / "bundle_linux_libs.py"
    subprocess.run(
        [
            "python3",
            str(script),
            "--binary",
            str(binary_path),
            "--lib-dir",
            str(lib_dir),
        ],
        check=True,
    )


def copy_docs(stage_root: Path, repo_root: Path) -> None:
    readme = repo_root / "README.md"
    if readme.is_file():
        shutil.copy2(readme, stage_root / "README.md")


def create_archive(stage_dir: Path, output_dir: Path, archive_base: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{archive_base}.tar.gz"
    with tarfile.open(archive_path, "w:gz") as archive:
        archive.add(stage_dir, arcname=stage_dir.name)
    return archive_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Package teez release binary")
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=["linux", "macos"])
    parser.add_argument("--arch", required=True, choices=["x86_64", "aarch64"])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--plugins-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    version = normalized_version(args.version)
    binary_path = Path(args.binary).resolve()
    plugins_dir = Path(args.plugins_dir).resolve()
    if not binary_path.is_file():
        raise FileNotFoundError(f"binary not found: {binary_path}")
    if not plugins_dir.is_dir():
        raise FileNotFoundError(f"plugins dir not found: {plugins_dir}")

    repo_root = Path(__file__).resolve().parents[2]
    output_dir = (repo_root / args.output_dir).resolve()
    archive_base = f"{PRODUCT}-{version}-{args.platform}-{args.arch}"
    stage_root = output_dir / archive_base
    if stage_root.exists():
        shutil.rmtree(stage_root)

    stage_bin_dir = stage_root / "bin"
    stage_bin_dir.mkdir(parents=True, exist_ok=True)
    binary_target = stage_bin_dir / PRODUCT
    shutil.copy2(binary_path, binary_target)
    copy_runtime_files(binary_path, stage_bin_dir, args.platform)
    copy_bundled_plugins(plugins_dir, stage_root)
    if args.platform == "linux":
        bundle_linux_libraries(binary_target, stage_root / "lib", repo_root)
    copy_docs(stage_root, repo_root)

    archive_path = create_archive(stage_root, output_dir, archive_base)
    print(archive_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
