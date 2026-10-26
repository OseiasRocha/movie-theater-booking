# C++ source documentation

The server exposes JSON REST endpoints using Drogon and stores configured movies,
theaters, and screenings in memory. Swagger UI at `/docs` describes the HTTP API;
this documentation describes its C++ implementation.

## Startup and configuration

`main()` parses `--address`, `--port`, and `--log-level`, then initializes
config::Configs before db::DB. Each supplied option skips its matching environment
variable. Omitted options fall back to environment variables and then defaults.
The first successful singleton initialization determines settings for subsequent
calls, including calls without parameters.

The database file comes from `CONFIG_DB_FILE_PATH`, defaulting to `config/db.json`
relative to the working directory. Parsing and validation reject malformed
records, duplicate IDs, missing movie/theater references, and invalid seat arrays.
Movies and theaters may have duplicate display names. There is no separate
server-configuration JSON loader.

## Storage, ownership, and locking

db::Storage owns maps of datamodel::Movie, datamodel::Theater, and
datamodel::Screening. db::DB::query() chooses shared or exclusive locking from the
operation's exact `execute` signature. The mutex covers execution and construction
of the returned result. The query objects themselves do not acquire locks.

Read operations return owning snapshots. Screening-list queries return
queries::ScreeningSummary values containing only identifiers, avoiding copies of
seat arrays. Seat availability is fetched through its dedicated query and endpoint. A returned pointer or view into storage
would escape the lock, which the concepts do not prevent. Queries must not call
back into the same database while executing. An exclusive lock serializes writes
but does not automatically roll back exceptions after a mutation.

Model name accessors return borrowed `std::string_view` values. Keep the owning
record alive and unchanged while using a view. A query such as
queries::GetMovieByName copies its input string so it can outlive the caller's input.

## Booking invariants

queries::MakeBooking executes under the database's exclusive lock. A screening
validates all requested labels before changing any seat: empty, oversized,
duplicate, unknown, or already booked requests fail without changing seat state.
No second booking can interleave with that validation and mutation through DB.

The configured seat count is fixed. datamodel::Screening::availableSeats() copies
only configured, unbooked seats. `allSeats()` currently copies the complete
fixed-capacity array, including unused default-constructed entries. A screening
has no internal mutex; direct users must provide synchronization when sharing it.
All changes are in memory and are lost when the process exits.

## HTTP and logging

`setupEndpoints()` registers the application handlers; api::setupDocumentation()
registers embedded Swagger and OpenAPI assets. Handlers capture their logger by
value, validate external input, execute DB queries, and deliver JSON through the
Drogon response callback. The generated specification is maintained in
`docs/openapi.json` and embedded into the executable by CMake.

Main, Config, Queries, Screening, and Drogon use registered spdlog loggers. Startup
applies one global pattern and the configured logging level. Trantor's trace
threshold allows forwarded Drogon records to be filtered by spdlog. Formatting a
trace message's arguments still incurs work before the logging call checks its level.

## Regression tests

The @ref unit_tests cover configuration validation, independent model snapshots,
booking rejection, competing writes, and readers observing whole booking updates.
Singleton tests initialize Configs and DB in child processes so cases do not share
startup state. Concurrency cases join all workers before checking results.

`tests/openapi_test.py` separately exercises the embedded specification, UI routes,
query validation, screening filtering, and booking responses over localhost HTTP
using temporary database fixtures.
