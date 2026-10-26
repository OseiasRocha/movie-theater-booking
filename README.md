# Movie theater booking

A C++20 starter application with portable threading support for Linux and
Windows on 32-bit x86 and x86-64. It prints atomic lock-free capabilities and
demonstrates a seat reservation that reduces availability from 100 to 98.
The `booking::reserve_seats` implementation lives in `src/main.cpp`; invalid
requests leave the available seat count unchanged.

## Prerequisites

- A C++20 compiler and standard library supporting `std::jthread`
  (for example GCC 11+ or Visual Studio 2022)
- CMake 3.23 or newer, Ninja, and Python 3
- Linux: development libraries for the target architecture, including libatomic
  where required; install `g++-multilib` on Debian/Ubuntu for 32-bit builds
- Windows: Visual Studio C++ build tools and a Windows SDK; use an **x64 Native
  Tools** terminal for x86-64 or an **x86 Native Tools** terminal for 32-bit x86

## Linux build and run

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt ninja
conan profile detect
conan install . --build=missing -s arch=x86_64 -s build_type=Release -s compiler.cppstd=20
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/movie-theater-booking
```

## Server address and port

```sh
./build/movie-theater-booking --address 127.0.0.1 --port 9000 --log-level debug
```

Each option overrides its matching environment variable (`ADDRESS` or `PORT`).
When an option is supplied, that environment variable is not read. Omitted
options use the environment, then default to `0.0.0.0` and `8080` when unset.
Ports must be integers from 1 to 65535. Empty addresses, unknown options,
duplicate options, and missing option values are rejected.

`--log-level` overrides `LOG_LEVEL` without reading it; when neither is set,
the default is `info`. Supported levels are `trace`, `debug`, `info`, `warning`
(`warn`), `error` (`err`), `critical`, and `off`.

Command-line options initialize `Configs` before the DB; subsequent singleton
access reuses those settings. `CONFIG_DB_FILE_PATH` continues to use its
environment variable independently.

## API documentation

With the server running, open `http://localhost:8080/docs` for Swagger UI.
Use your configured port if different. The OpenAPI 3.0 specification is served
at `/openapi.json`; Swagger's **Try it out** calls the same server.

The documentation covers `GET /movies`, `GET /theaters`, `GET /theater`,
`GET /screenings`, `GET /available_seats`, and `POST /bookings`, including
parameters, JSON schemas, examples, and error responses. `/screenings` returns
full records without filters, or matching IDs when both `movieId` and
`theaterId` are supplied.

Edit `docs/openapi.json` when changing the API. CMake embeds the specification
and `docs/swagger.html` in the executable and regenerates them on rebuild.
The browser loads Swagger UI 5.17.14 from jsDelivr, so the interactive UI needs
internet access; `/openapi.json` is available without it. The external online
specification validator is disabled.

Run the documentation/API integration tests with Python 3 (standard library
only). They start a separate localhost server with temporary database data:

```sh
cmake --build build --target movie-theater-booking
python3 tests/openapi_test.py build/movie-theater-booking
```

## Windows build and run

In a Visual Studio developer PowerShell terminal for the target architecture:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt ninja
conan profile detect
conan install . --build=missing -s arch=x86_64 -s build_type=Release -s compiler.cppstd=20
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\movie-theater-booking.exe
```

For 32-bit builds on either OS, replace `-s arch=x86_64` with `-s arch=x86`.
On Windows, also select the x86 developer terminal. Use a clean build directory
when switching architectures or compilers. These commands build for the host OS;
cross-compiling Windows binaries from Linux requires a separate toolchain and
matching Conan profiles.

For Debug, use `-s build_type=Debug` during Conan installation and
`-DCMAKE_BUILD_TYPE=Debug` during CMake configuration. Keep these settings aligned.

Conan generates the toolchain and dependency configuration in `build/generators`.
CMake loads that toolchain automatically unless you explicitly supply
`CMAKE_TOOLCHAIN_FILE`. CMake discovers the platform's native threading support
through `Threads::Threads` and explicitly links `libatomic` on Linux. Lock-free results may differ by compiler and architecture;
correct operation does not require lock-free aggregate atomics.

Conan can also build an already configured directory. Match the settings used
when installing dependencies, for example:

```sh
conan build . -s build_type=Release -s compiler.cppstd=20
```

If you prefer `conan build . -s build_type=Debug` with your profile's default
C++ standard, first install matching dependencies with
`conan install . --build=missing -s build_type=Debug`, then configure Debug
with CMake. The application still compiles as C++20. Omitting
`-s compiler.cppstd=20` only from the build command can cause a missing GoogleTest
binary error because Conan resolves a different package configuration.

The generated Conan presets are available after dependency installation. CMake exports
`compile_commands.json` for editor support. Build outputs and generated presets
are ignored by Git.

## Formatting

The root `.clang-format` controls C/C++ formatting; replace it with your preferred
style. In your current Neovim setup, Mason installs `clang-format` and Conform
runs it on save for C/C++ files. Use `<leader>f` to format manually or
`:ConformInfo` to inspect formatter availability. Restart Neovim after setup changes.

This checkout has a local Python 3 hook in `.git/hooks/pre-commit`. It is not
versioned or included when cloning the repository. It automatically formats
staged `.h` and `.cpp` changes using the
root `.clang-format` and commits the formatted result. It uses `clang-format`
from PATH or Neovim's Mason installation. Fully staged files are also updated in
your working copy. For partially staged files, the working copy is left untouched
so unstaged edits are preserved; Git may then show formatting differences against
the new commit. Deleted files and files without staged changes are skipped.
Formatter errors stop the commit.

## Unit tests

Conan provides GoogleTest as a test dependency. CMake builds `unit-tests` by
default and registers each test with CTest using `gtest_discover_tests`.
Six tests exercise the seat reservation implementation in `src/main.cpp`,
including successful bookings, sellouts, invalid requests, and consecutive bookings.
The test target defines `MOVIE_THEATER_BOOKING_NO_MAIN` to use GoogleTest's entry
point while compiling the same booking function as the application.

After adding or changing dependencies, reinstall them before configuring:

```sh
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

List tests with `ctest --test-dir build -N`, or select tests with
`ctest --test-dir build -R SeatBooking --output-on-failure`.
Use `cmake -S . -B build -DBUILD_TESTING=OFF` to disable building the tests.

## Docker

The multi-stage Dockerfile uses Docker Hardened Images' Alpine 3.23 development
and runtime images. It builds all Conan dependencies from source against musl,
uses Alpine's system CMake, and runs GoogleTest before producing the runtime
image. The runtime contains the application and its C++ runtime libraries and
runs as UID/GID 65532.

```sh
docker login dhi.io
docker build -t movie-theater-booking .
docker run --rm movie-theater-booking
```

Docker documents registry authentication for these images; see the
[Docker Hardened Images guide](https://docs.docker.com/dhi/how-to/use/).
For CI, configure repository Actions secrets `DHI_USERNAME` (Docker Hub username)
and `DHI_TOKEN` (an access token with pull access). The workflow logs in when
both secrets are available; otherwise it attempts the pull without login.
Fork pull requests do not receive repository secrets and may fail to pull
the base images if authentication is required.

## Continuous integration

GitHub Actions runs on pull requests and published GitHub Releases. Opening,
reopening, and updating an open pull request trigger CI. Pushes without a pull
request and draft releases do not trigger it.
The build matrix tests Linux x86-64, Windows x86-64, and Linux ARM64. A separate
Docker job builds the hardened Alpine image, runs unit tests during the build,
and checks that the final container starts successfully. Pull requests build
and test without publishing.

## Release publishing

Publishing a GitHub Release (including a prerelease) runs the same CI. After
all builds and tests pass, the workflow publishes:

- Linux x86-64 and ARM64 `.tar.gz` binary archives and a Windows x86-64 `.zip`
  archive, each with a `.sha256` checksum, attached to that GitHub Release.
- The tested hardened Alpine Docker image for `linux/amd64`, tagged with the
  release tag, such as `v1.0.0`. Stable releases also update `latest`;
  prereleases do not.

Configure these repository Actions secrets before publishing a release:

| Secret | Value |
| --- | --- |
| `DHI_USERNAME` | Docker Hub username for pulling hardened base images |
| `DHI_TOKEN` | Token with read access to hardened base images |
| `DOCKERHUB_USERNAME` | Docker Hub username for publishing |
| `DOCKERHUB_TOKEN` | Docker Hub token with write access to the image repository |

The image defaults to `<DOCKERHUB_USERNAME>/movie-theater-booking`. To publish
under another repository or organization, set the Actions variable
`DOCKERHUB_IMAGE` to the full image name, such as `my-org/movie-theater-booking`,
and grant the publishing token write access there. Create the Docker Hub
repository if needed. The GitHub release upload uses the workflow's automatic
`GITHUB_TOKEN`; no GitHub personal token is needed.

Release archives contain `bin/` and this README. Windows archives include the
MSVC runtime DLLs detected by CMake. Linux binaries require the target system's
glibc, libstdc++, libgcc, and libatomic; they are built on the Ubuntu runner
listed in the workflow. Use the Docker image for the Alpine/musl build.

The workflow uploads binaries using `gh release upload --clobber`, allowing a
failed release run to be rerun with the same asset names. Saving a draft does
not publish anything; select **Publish release** to start the pipeline.
