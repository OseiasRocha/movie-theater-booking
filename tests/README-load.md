# HTTP load test

Requires Linux, Python 3.10+, Docker, and the built runtime image:

```sh
python -m pip install -r tests/requirements-load.txt
docker build -t movie-theater-booking:local .
python tests/load_test.py
```

Defaults are **1,000 scheduled requests/second for 30 seconds**: 27,000 GETs
and 3,000 POSTs. The test starts an isolated container on a random localhost
port and removes it afterward. It generates its own database and mounts it
read-only; the repository's database and existing servers are not modified.
The default fixture has 10 movies, 10 theaters, 250 screenings, and 5,000 seats.

GETs cover `/movies`, `/theaters`, `/theater`, filtered `/screenings`,
`/available_seats`, and unfiltered `/screenings` summaries. Each group of ten requests has nine
GETs followed by one POST. POSTs use unique seats and expect success; deliberate
booking conflicts are not included in the measured workload. Immutable screenings
provide stable GET expectations while other screenings receive bookings. Response
validation ignores unordered-map iteration order and checks IDs, names, filtering,
seat labels, and booking response contents. Final verification checks persistence
of every acknowledged booking through `/available_seats` after the timed load
has drained; the listing intentionally contains no booking state.

The scheduler uses absolute arrival times, independently of response speed.
It caps in-flight work at 8,192 requests and drops arrivals more than 100 ms
late, reporting those separately rather than slowing the target rate. The final
scheduled arrivals may start up to that tolerance after the planned 30-second
window; the report includes the actual last start time and achieved rate. No application-level retries are
performed; aiohttp may retry idempotent requests on stale connections.

Results in `build/load-test/`:

- `report.json`: planned/achieved rate, actual sends per second, method counts,
  HTTP statuses, incorrect responses, unsent requests, timeouts/transport errors,
  mean/p50/p95/p99/max latency overall and per endpoint, scheduler lag, booking
  persistence checks, generator CPU usage, and server Docker CPU/RAM samples.
- `requests.csv`: one row per attempted request, including errors and latency.
- `db.json`: generated reproducible fixture.
- `server.log`: captured server output (error-level logging during the test).

Response latency measures client request start through complete body receipt,
before decompression or JSON validation. Compressed response bodies are buffered
and checked after traffic drains so validation cannot stall the scheduler. Scheduling lag is measured
separately; scheduled-to-response latency is also reported. Failed attempts are
included in a separate attempt-duration distribution, not silently averaged into
successful HTTP response times. Warmup, startup, and final verification are
outside the measured workload. Requests still in flight after 30 seconds drain
up to their configured timeout.

**No complete response is not proof of packet loss.** A timed-out POST may have
committed; the final state check reports bookings without a valid acknowledgment
separately. Incorrect HTTP statuses, malformed JSON, and incorrect response bodies
count as wrong answers. Client-capacity drops are unsent work, not server losses.
The test returns nonzero for any unsent, unanswered, incorrect, or inconsistent
booking result.

Logging is set to error. Unguarded formatting passed to disabled trace calls
would still consume CPU; rebuild the image after changing server code.

Client and server share the host, so results include client/server CPU contention
and container networking. Docker CPU percentages use 100% per logical CPU. The unfiltered
screening-summary listing remains part of the default workload; its response
contains identifiers only, with no seat arrays.

For a short check or a different rate:

```sh
python tests/load_test.py --rate 100 --duration 2 --output build/load-test-smoke
python tests/load_test.py --small-gets-only --output build/load-test-small-gets
python tests/load_test_unit.py
```

`--small-gets-only` replaces unfiltered screening-summary GETs with movie GETs while preserving
the database size and 90/10 method split, isolating the cost of large responses.

Use `--help` for the in-flight limit, timeout, scheduling-lag limit, output path,
and image tag. Reusing an output directory overwrites its previous report.
