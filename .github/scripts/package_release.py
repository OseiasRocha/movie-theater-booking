"""Archive the CMake installation and write its SHA-256 checksum."""

import argparse
import hashlib
from pathlib import Path
import tarfile
import zipfile


def package_release(platform, arch, staging, output):
    executable = "movie-theater-booking.exe" if platform == "Windows" else "movie-theater-booking"
    if not (staging / "bin" / executable).is_file():
        raise FileNotFoundError(f"Run cmake --install first: missing {staging / 'bin' / executable}")

    output.mkdir(parents=True, exist_ok=True)
    name = f"movie-theater-booking-{platform.lower()}-{arch}"
    extension = ".zip" if platform == "Windows" else ".tar.gz"
    archive = output / (name + extension)
    files = [(path, Path(name) / path.relative_to(staging))
             for path in sorted(staging.rglob("*")) if path.is_file()]
    files.append((Path("README.md"), Path(name) / "README.md"))

    if platform == "Windows":
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for source, destination in files:
                bundle.write(source, destination.as_posix())
    else:
        with tarfile.open(archive, "w:gz") as bundle:
            for source, destination in files:
                bundle.add(source, arcname=destination.as_posix())

    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + ".sha256").write_text(
        f"{digest}  {archive.name}\n", encoding="utf-8"
    )
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("Linux", "Windows"))
    parser.add_argument("--arch", required=True, choices=("x86_64", "armv8"))
    parser.add_argument("--staging", type=Path, default=Path("package"))
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    print(package_release(args.platform, args.arch, args.staging, args.output))
