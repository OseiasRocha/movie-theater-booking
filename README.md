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

GitHub Actions runs only on pull requests: opening, reopening, and updating an
open pull request trigger CI. Pushes without a pull request do not trigger it.
The build matrix tests Linux x86-64, Windows x86-64, and Linux ARM64. A separate
Docker job builds the hardened Alpine image, runs unit tests during the build,
and checks that the final container starts successfully. Images are built
locally in the runner and are not published.
