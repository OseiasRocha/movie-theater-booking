# Movie Theater Booking

C++20 REST API built with Drogon and an in-memory booking database.

## Requirements

C++20 compiler, CMake 3.23+, Ninja, and Python 3. On Windows, use a Visual Studio developer terminal.

## Configure and build

Run from the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt ninja
conan profile detect
conan install . --build=missing -s build_type=Release -s compiler.cppstd=20
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, use `python` instead of `python3` and activate with `.venv\Scripts\Activate.ps1` in PowerShell.
Run `conan profile detect` only for initial setup. For a Debug build, replace `Release` with `Debug` in both commands.

## Run and configure

```sh
./build/movie-theater-booking --address 127.0.0.1 --port 8080 --log-level info
```

On Windows, run `.\build\movie-theater-booking.exe` with the same options.

Command-line options override their matching environment variables; omitted settings use the environment, then defaults.

| Option | Environment variable | Default |
| --- | --- | --- |
| `--address` | `ADDRESS` | `0.0.0.0` |
| `--port` | `PORT` | `8080` |
| `--log-level` | `LOG_LEVEL` | `info` |
| — | `CONFIG_DB_FILE_PATH` | `config/db.json` |

Ports must be 1–65535. Log levels: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`.
Application logs use spdlog; Drogon/Trantor use their native format. The level sets both thresholds,
but Trantor always emits warnings and errors unless the level is `off`.
Edit `config/db.json` to configure movies, theaters, screenings, and seats. Paths are relative to the working directory; the build also copies `config/` into `build/`. Restart the server after changing configuration.

Swagger UI: `http://localhost:8080/docs` (use your configured port; UI assets require internet).
OpenAPI: `/openapi.json`. Generate C++ documentation with `doxygen Doxyfile`, then open `build/doxygen/html/index.html`.

## Docker

```sh
docker build -t movie-theater-booking:local .
docker run --rm -p 8080:8080 movie-theater-booking:local
```

The image includes `config/db.json`, runs as a non-root user, and accepts the same CLI options and environment variables.

## Load test

Install `tests/requirements-load.txt` and run `python tests/load_test.py` to test
1,000 requests/second for 30 seconds (90% GET, 10% POST) in an isolated container.
See [test instructions and metric definitions](tests/README-load.md); results go to `build/load-test/`.
