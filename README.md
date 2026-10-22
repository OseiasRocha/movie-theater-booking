# C++20 template

A small C++20 executable using CMake, Ninja, Conan 2, and spdlog 1.17.0.

## Prerequisites

- A C++20 compiler (GCC, Clang, or MSVC)
- CMake 3.23 or newer
- Ninja on PATH
- Python 3 with pip and venv

On Windows, run the build commands from a Visual Studio developer terminal.

## Build and run

From this directory, install Conan in a virtual environment:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

On Windows, use `python` instead of `python3`, and activate with
`.venv\Scripts\Activate.ps1` in PowerShell.

Detect a default profile once (skip this if you already have one), install
Conan dependencies, and configure with CMake:

```sh
conan profile detect
conan install . --build=missing
cmake -S . -B build -G Ninja
conan build .
./build/cpp20_template
```

Conan writes its dependency configuration to `build/generators`. CMake loads
that toolchain automatically before `project()`, so this also works:

```sh
cd build
cmake ..
cmake --build .
```

`conan build .` runs the build for the existing CMake configuration; its
`build()` method does not call configure. Conan still performs its normal
dependency resolution and generator steps, and Ninja may automatically rerun
CMake when inputs change. Run CMake explicitly before your first build.

The executable and `compile_commands.json` are directly in `build/`.
CMake always compiles the application as C++20; dependencies use the Conan
profile's standard. Pass `-s compiler.cppstd=20` to both Conan commands if you
also want dependencies built with C++20.

For Debug, install with `conan install . --build=missing -s build_type=Debug`,
configure with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, and build with
`conan build . -s build_type=Debug`. Keep Conan and CMake build types aligned.
Without an explicit CMake build type, CMake uses the type from Conan install.
On Windows the executable has an `.exe` extension.

When migrating from the old `build/Release` layout, rerun `conan install .`
and configure with `cmake --fresh -S . -B build -G Ninja` once to replace the
old CMake cache.

## CMake presets

Conan generates `CMakeUserPresets.json` and the toolchain in `build/`. After
the Release dependency installation above, you can also build directly:

```sh
cmake --preset conan-release
cmake --build --preset conan-release
```

The Conan presets select Ninja. Generated files and build outputs are ignored
by Git. Edit `src/main.cpp` to start developing; add Conan requirements in
`conanfile.py` and link their imported targets in `CMakeLists.txt`.

References: [Conan build command](https://docs.conan.io/2/reference/commands/build.html)
and [spdlog on ConanCenter](https://conan.io/center/recipes/spdlog).
