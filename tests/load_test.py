"""Rate-controlled HTTP load test against an isolated Docker container.

Default: 30,000 scheduled requests over 30 seconds, exactly 90% GET / 10% POST.
Install tests/requirements-load.txt, build the image, then run this script.
The fixture and full results are saved under build/load-test; production data is
never contacted. POSTs use unique seats, so every workload request expects 200.
"""

import argparse
import asyncio
from collections import Counter, defaultdict
import csv
import gzip
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import resource
import subprocess
import time
import zlib

import aiohttp


SEATS = [f"{row}{number}" for row in "ABCD" for number in range(1, 6)]
GET_ROUTES = ("movies", "theaters", "theater", "filtered", "available",
              "movies", "theaters", "theater", "screenings")


def fixture(total):
    """Create immutable read fixtures and enough separate seats for all POSTs."""
    data = {
        "movies": [{"id": i, "name": f"Movie {i}"} for i in range(1, 11)],
        "theaters": [{"id": i, "name": f"Theater {i}"} for i in range(10, 20)],
        "screenings": [],
    }
    count = 100 + math.ceil((total // 10) / len(SEATS))
    for i in range(count):
        data["screenings"].append({
            "id": 100 + i if i < 100 else 1000 + i - 100,
            "movieId": i % 10 + 1,
            "theaterId": (i // 10) % 10 + 10,
            "seats": [{"name": seat} for seat in SEATS],
        })
    return data


def operation(index):
    """Use a deterministic 9 GET / 1 POST cycle and never reuse a booking seat."""
    cycle, slot = divmod(index, 10)
    if slot == 9:
        return "POST", "/bookings", {"screeningId": 1000 + cycle // 20,
                                     "seats": [SEATS[cycle % 20]]}
    movie, theater = cycle % 10 + 1, (cycle // 10) % 10 + 10
    routes = {
        "movies": "/movies", "theaters": "/theaters", "screenings": "/screenings",
        "theater": f"/theater?movieId={movie}",
        "filtered": f"/screenings?movieId={movie}&theaterId={theater}",
        "available": "/available_seats?screeningId=100",
    }
    return "GET", routes[GET_ROUTES[slot]], None


class Oracle:
    """Check full response contents without relying on unordered-map ordering."""

    def __init__(self, data):
        self.screenings = {s["id"]: s for s in data["screenings"]}
        self.movies = {m["id"]: {"id": m["id"], "movie": m["name"]} for m in data["movies"]}
        self.theaters = {t["id"]: t for t in data["theaters"]}
        self.filtered = defaultdict(set)
        self.movie_theaters = defaultdict(set)
        for s in data["screenings"]:
            self.filtered[s["movieId"], s["theaterId"]].add(s["id"])
            self.movie_theaters[s["movieId"]].add(s["theaterId"])

    @staticmethod
    def records(body):
        if not isinstance(body, list) or any(not isinstance(x, dict) or type(x.get("id")) is not int for x in body):
            raise ValueError("Expected an array of records with integer IDs")
        result = {x["id"]: x for x in body}
        if len(result) != len(body):
            raise ValueError("Duplicate record IDs")
        return result

    def check(self, index, body):
        method, path, payload = operation(index)
        if method == "POST":
            if body != {"booked": True, **payload} or type(body.get("booked")) is not bool:
                raise ValueError("Booking response does not match the requested screening/seat")
            return
        route = GET_ROUTES[index % 10]
        cycle = index // 10
        movie, theater = cycle % 10 + 1, (cycle // 10) % 10 + 10
        if route == "available":
            expected = [{"seat": name, "isBooked": False} for name in SEATS]
            if body != expected or any(type(s.get("isBooked")) is not bool for s in body):
                raise ValueError("Immutable screening's available seats are incorrect")
            return
        records = self.records(body)
        if route == "movies":
            expected = self.movies
        elif route == "theaters":
            expected = self.theaters
        elif route == "theater":
            expected = {i: self.theaters[i] for i in self.movie_theaters[movie]}
        elif route == "filtered":
            expected = {i: {"id": i} for i in self.filtered[movie, theater]}
        else:
            self.check_screenings(body)
            return
        if records != expected:
            raise ValueError(f"Incorrect response records for {path}")

    def check_screenings(self, body):
        """Validate lightweight screening summaries without copying any seat state."""
        records = self.records(body)
        expected = {ident: {key: row[key] for key in ("id", "movieId", "theaterId")}
                    for ident, row in self.screenings.items()}
        if records != expected or any(type(row.get(key)) is not int for row in records.values()
                                      for key in ("movieId", "theaterId")):
            raise ValueError("Incorrect screening summaries, or unexpected seat data")


def decode_body(raw, encoding):
    if encoding == "gzip":
        return gzip.decompress(raw)
    if encoding in ("", "identity"):
        return raw
    raise ValueError(f"Unsupported content encoding: {encoding}")


def validate_responses(payloads, oracle):
    """Decompress and validate after traffic drains, so checking cannot stall arrivals."""
    for result, raw, encoding in payloads:
        try:
            decoded = decode_body(raw, encoding)
            result.bytes_received = len(decoded)
            if result.outcome != "ok":
                continue
            body = json.loads(decoded)
        except (ValueError, UnicodeError, OSError, EOFError, zlib.error) as error:
            if result.outcome == "ok":
                result.outcome, result.detail = "invalid_json", str(error)
        else:
            try:
                oracle.check(result.index, body)
            except (ValueError, TypeError, AttributeError, KeyError) as error:
                result.outcome, result.detail = "wrong_body", str(error)


@dataclass
class Result:
    index: int
    method: str
    path: str
    status: int | None
    latency_ms: float
    schedule_lag_ms: float
    bytes_received: int
    outcome: str
    detail: str = ""


def distribution(values):
    if not values:
        return {"count": 0, "mean": None, "p50": None, "p95": None, "p99": None, "max": None}
    values = sorted(values)
    return {"count": len(values), "mean": sum(values) / len(values),
            **{f"p{p}": values[max(0, math.ceil(len(values) * p / 100) - 1)] for p in (50, 95, 99)},
            "max": values[-1]}


async def run_load(session, base, args):
    """Absolute arrival deadlines avoid slowing the offered rate when the server slows."""
    loop = asyncio.get_running_loop()
    results, pending, payloads = [], set(), []
    unsent = Counter()
    failures = []
    in_flight_peak = 0

    def completed(task):
        pending.discard(task)
        if not task.cancelled() and task.exception() is not None:
            failures.append(task.exception())

    async def send(index, due):
        method, path, payload = operation(index)
        started = loop.time()
        if started - due > args.max_lag:
            unsent["scheduler_late"] += 1
            return
        status, size, outcome, detail = None, 0, "ok", ""
        try:
            async with session.request(method, base + path, json=payload) as response:
                status = response.status
                raw = await response.read()
                size = len(raw)
                finished = loop.time()
                if status != 200:
                    outcome, detail = "wrong_status", f"Expected 200, got {status}"
        except asyncio.TimeoutError:
            finished, outcome, detail = loop.time(), "timeout", "Client request timeout"
        except aiohttp.ClientError as error:
            finished, outcome, detail = loop.time(), "transport_error", str(error)
        result = Result(index, method, path, status, (finished - started) * 1000,
                        max(0, started - due) * 1000, size, outcome, detail)
        results.append(result)
        if outcome not in ("timeout", "transport_error"):
            payloads.append((result, raw, response.headers.get("Content-Encoding", "").lower()))


    started = loop.time()
    for index in range(args.rate * args.duration):
        due = started + index / args.rate
        if (delay := due - loop.time()) > 0:
            await asyncio.sleep(delay)
        now = loop.time()
        if now - due > args.max_lag:
            unsent["scheduler_late"] += 1
        elif len(pending) >= args.max_in_flight:
            unsent["in_flight_limit"] += 1
        else:
            task = asyncio.create_task(send(index, due))
            pending.add(task)
            task.add_done_callback(completed)
            in_flight_peak = max(in_flight_peak, len(pending))
    await asyncio.sleep(max(0, started + args.duration - loop.time()))
    pending_at_end = len(pending)
    if pending:
        await asyncio.gather(*pending, return_exceptions=True)
    if failures:
        raise RuntimeError("Load-generator task failed") from failures[0]
    return results, unsent, loop.time() - started, pending_at_end, in_flight_peak, payloads


async def sample_resources(container, samples):
    """Collect server CPU/RAM from Docker without blocking the arrival scheduler."""
    process = await asyncio.create_subprocess_exec(
        "docker", "stats", "--format", "{{json .}}", container,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
    try:
        async for line in process.stdout:
            # Streaming docker stats can prefix samples with terminal-clear sequences.
            start = line.find(b"{")
            if start >= 0:
                try:
                    samples.append(json.JSONDecoder().raw_decode(line.decode(), start)[0])
                except ValueError:
                    samples.append({"error": "Unparseable Docker stats sample", "raw": repr(line[:300])})
    finally:
        if process.returncode is None:
            process.terminate()
        await process.wait()


async def exercise(container, port, data, args):
    base = f"http://127.0.0.1:{port}"
    oracle = Oracle(data)
    timeout = aiohttp.ClientTimeout(total=args.timeout, ceil_threshold=float("inf"))
    connector = aiohttp.TCPConnector(limit=args.max_in_flight)
    async with aiohttp.ClientSession(timeout=timeout, connector=connector, auto_decompress=False,
                                     headers={"Accept-Encoding": "gzip"}) as session:
        for attempt in range(100):
            try:
                async with session.get(base + "/movies") as response:
                    if response.status == 200:
                        break
            except aiohttp.ClientError:
                pass
            await asyncio.sleep(.1)
        else:
            raise RuntimeError("Container did not become ready")
        # Warm all read handlers, without consuming booking seats or measurement slots.
        for index in range(9):
            _, path, _ = operation(index)
            async with session.get(base + path) as response:
                if response.status != 200:
                    raise RuntimeError(f"Warmup failed for {path}: {response.status}")
                oracle.check(index, json.loads(decode_body(await response.read(), response.headers.get("Content-Encoding", ""))))
        samples = []
        monitor = asyncio.create_task(sample_resources(container, samples))
        cpu_before = resource.getrusage(resource.RUSAGE_SELF)
        try:
            results, unsent, elapsed, pending, peak, payloads = await run_load(session, base, args)
        finally:
            monitor.cancel()
            try:
                await monitor
            except asyncio.CancelledError:
                pass
            except Exception as error:
                samples.append({"error": f"Resource monitoring failed: {error}"})
        cpu_after = resource.getrusage(resource.RUSAGE_SELF)
        validation_started = time.perf_counter()
        validate_responses(payloads, oracle)
        validation_seconds = time.perf_counter() - validation_started
        payloads.clear()
        # Check persistence only after every sent request has completed or timed out.
        expected_bookings = {(operation(r.index)[2]["screeningId"], operation(r.index)[2]["seats"][0])
                             for r in results if r.method == "POST" and r.outcome == "ok"}
        verification = {"acknowledged_bookings": len(expected_bookings), "persisted_bookings": None,
                        "acknowledged_but_missing": [], "booked_without_valid_acknowledgment": [],
                        "error": None}
        try:
            async with session.get(base + "/screenings") as response:
                if response.status != 200:
                    raise ValueError(f"Final verification returned HTTP {response.status}")
                body = json.loads(decode_body(await response.read(), response.headers.get("Content-Encoding", "")))
                oracle.check_screenings(body)
                final = oracle.records(body)
            # Summaries intentionally omit seat state. Verify through the dedicated API.
            semaphore = asyncio.Semaphore(16)

            async def booked_seats(ident):
                async with semaphore:
                    async with session.get(base + f"/available_seats?screeningId={ident}") as response:
                        if response.status != 200:
                            raise ValueError(f"Availability verification failed for {ident}")
                        seats = json.loads(decode_body(await response.read(), response.headers.get("Content-Encoding", "")))
                    if not isinstance(seats, list):
                        raise ValueError(f"Invalid availability response for {ident}")
                    names = [seat["seat"] for seat in seats]
                    if any(type(seat.get("isBooked")) is not bool for seat in seats):
                        raise ValueError(f"Nonboolean booking state for {ident}")
                    if len(names) != len(set(names)) or not set(names) <= set(SEATS):
                        raise ValueError(f"Incorrect available seat labels for {ident}")
                    if seats != [{"seat": name, "isBooked": False} for name in SEATS if name in names]:
                        raise ValueError(f"Incorrect availability order/state for {ident}")
                    return {(ident, name) for name in SEATS if name not in names}

            actual_bookings = set().union(*(await asyncio.gather(*(booked_seats(ident) for ident in final))))
            verification.update({
                "persisted_bookings": len(actual_bookings),
                "acknowledged_but_missing": sorted(expected_bookings - actual_bookings),
                "booked_without_valid_acknowledgment": sorted(actual_bookings - expected_bookings),
            })
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError, TypeError, KeyError) as error:
            verification["error"] = str(error) or type(error).__name__
        cpu_seconds = (cpu_after.ru_utime + cpu_after.ru_stime - cpu_before.ru_utime - cpu_before.ru_stime)
        return results, unsent, elapsed, pending, peak, verification, samples, cpu_seconds, validation_seconds


def summarize(results, unsent, elapsed, pending, peak, verification, samples, cpu_seconds, validation_seconds, args):
    if len(results) + sum(unsent.values()) != args.rate * args.duration:
        raise RuntimeError("Load generator did not account for every scheduled request")
    groups = defaultdict(list)
    for result in results:
        endpoint = result.path.split("?")[0]
        if endpoint == "/screenings" and "?" in result.path:
            endpoint += " (filtered)"
        groups[result.method + " " + endpoint].append(result)
    def metrics(records):
        return {
            "sent": len(records), "outcomes": dict(Counter(r.outcome for r in records)),
            "http_statuses": dict(Counter(str(r.status) for r in records if r.status is not None)),
            "response_latency_ms": distribution([r.latency_ms for r in records
                                                   if r.outcome not in ("timeout", "transport_error")]),
            "scheduled_to_response_ms": distribution([r.latency_ms + r.schedule_lag_ms for r in records
                                                         if r.outcome not in ("timeout", "transport_error")]),
            "attempt_duration_ms": distribution([r.latency_ms for r in records]),
            "schedule_lag_ms": distribution([r.schedule_lag_ms for r in records]),
        }
    wrong = sum(r.outcome in ("wrong_status", "wrong_body", "invalid_json") for r in results)
    no_response = sum(r.outcome in ("timeout", "transport_error") for r in results)
    return {
        "target": {"requests_per_second": args.rate, "duration_seconds": args.duration,
                   "planned": args.rate * args.duration, "get_percent": 90, "post_percent": 10},
        "settings": {"max_in_flight": args.max_in_flight, "request_timeout_seconds": args.timeout,
                     "max_scheduler_lag_seconds": args.max_lag, "logging_level": "error",
                     "screening_summaries_in_timed_mix": not args.small_gets_only, "get_cycle": GET_ROUTES},
        "observed": {"sent": len(results), "get_sent": sum(r.method == "GET" for r in results),
                     "post_sent": sum(r.method == "POST" for r in results),
                     "responded": len(results) - no_response, "no_complete_response": no_response,
                     "wrong_answers": wrong, "not_sent": sum(unsent.values()), "not_sent_reasons": dict(unsent),
                     "send_rate_per_second": len(results) / max(args.duration,
                         max((r.index / args.rate + r.schedule_lag_ms / 1000 for r in results), default=0)),
                     "last_request_started_seconds": max((r.index / args.rate + r.schedule_lag_ms / 1000
                                                          for r in results), default=None),
                     "completed_responses_per_second_including_drain": (len(results) - no_response) / elapsed,
                     "total_seconds_including_drain": elapsed, "validation_seconds_after_load": validation_seconds, "in_flight_at_window_end": pending,
                     "peak_in_flight": peak,
                     "actual_sends_per_second": dict(sorted(Counter(int(r.index / args.rate + r.schedule_lag_ms / 1000)
                                                                     for r in results).items())), "decoded_response_bytes": sum(r.bytes_received for r in results)},
        "overall": metrics(results), "per_endpoint": {k: metrics(v) for k, v in sorted(groups.items())},
        "booking_verification": verification,
        "resources": {"client_cpu_seconds": cpu_seconds,
                      "client_cpu_percent_of_one_core": 100 * cpu_seconds / elapsed,
                      "server_cpu_percent_of_one_core": distribution([float(s["CPUPerc"].rstrip("%"))
                                                                    for s in samples if s.get("CPUPerc")]),
                      "server_stats_sample_count": sum("CPUPerc" in s for s in samples),
                      "server_docker_stats": samples},
        "failure_examples": [asdict(r) for r in results if r.outcome != "ok"][:20],
        "notes": ["HTTP/1.1 keep-alive with gzip accepted; absolute arrival schedule; no application retries (aiohttp may retry idempotent requests on stale connections).",
                  "Latency is measured from client request start through complete compressed response-body receipt. Decompression and JSON validation run after the load window/drain.",
                  "Schedule lag and client-capacity drops are separate from server response latency.",
                  "No complete response means timeout/transport failure, not proven packet loss; a timed-out POST may still commit.",
                  "Final verification, warmup, and startup are outside the measured workload.",
                  "Client and server share the host. This measures the image/fixture workload, not remote network capacity."],
    }


def main():
    global GET_ROUTES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--small-gets-only", action="store_true",
                        help="Replace unfiltered /screenings GETs with /movies; keep the 90/10 split and fixture size")
    parser.add_argument("--image", default="movie-theater-booking:local")
    parser.add_argument("--rate", type=int, default=1000)
    parser.add_argument("--duration", type=int, default=30)
    parser.add_argument("--max-in-flight", type=int, default=8192)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--max-lag", type=float, default=.1)
    parser.add_argument("--output", type=Path, default=Path("build/load-test"))
    args = parser.parse_args()
    if args.rate <= 0 or args.rate % 10 or args.duration <= 0 or args.max_in_flight <= 0 or args.timeout <= 0 or args.max_lag <= 0:
        parser.error("rate must be a positive multiple of 10; duration, limits, and timeouts must be positive")
    if args.small_gets_only:
        GET_ROUTES = (*GET_ROUTES[:8], "movies")
    args.output.mkdir(parents=True, exist_ok=True)
    data = fixture(args.rate * args.duration)
    config = (args.output / "db.json").resolve()
    config.write_text(json.dumps(data))
    config.chmod(0o644)
    image = json.loads(subprocess.check_output(["docker", "image", "inspect", args.image], text=True))[0]
    container = subprocess.check_output([
        "docker", "run", "--detach", "--publish", "127.0.0.1::8080",
        "--mount", f"type=bind,source={config},target=/app/config/db.json,readonly",
        image["Id"], "--log-level", "error"], text=True).strip()
    print(f"Container {container[:12]}: scheduling {args.rate}/s for {args.duration}s, 90% GET / 10% POST", flush=True)
    try:
        info = json.loads(subprocess.check_output(["docker", "inspect", container], text=True))[0]
        port = int(info["NetworkSettings"]["Ports"]["8080/tcp"][0]["HostPort"])
        measured = asyncio.run(exercise(container, port, data, args))
        report = summarize(*measured, args)
        report["environment"] = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
                                 "image": args.image, "image_id": image["Id"], "aiohttp_version": aiohttp.__version__,
                                 "python_version": platform.python_version(),
                                 "platform": platform.platform(), "host_cpus": os.cpu_count(),
                                 "movies": len(data["movies"]), "theaters": len(data["theaters"]),
                                 "screenings": len(data["screenings"]), "seats_per_screening": len(SEATS)}
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        with (args.output / "requests.csv").open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(Result.__dataclass_fields__))
            writer.writeheader()
            writer.writerows(asdict(r) for r in sorted(measured[0], key=lambda r: r.index))
        print(json.dumps({"observed": report["observed"],
                          "latency_ms": report["overall"]["response_latency_ms"],
                          "booking_verification": {k: len(v) if isinstance(v, list) else v
                                                   for k, v in report["booking_verification"].items()}}, indent=2))
        print(f"Full report: {args.output / 'report.json'}; per-request data: {args.output / 'requests.csv'}")
        bad = report["observed"]
        return int(bool(bad["not_sent"] or bad["no_complete_response"] or bad["wrong_answers"] or report["booking_verification"]["error"] or
                        report["booking_verification"]["acknowledged_but_missing"] or
                        report["booking_verification"]["booked_without_valid_acknowledgment"]))
    finally:
        logs = subprocess.run(["docker", "logs", container], capture_output=True, text=True)
        (args.output / "server.log").write_text(logs.stdout + logs.stderr)
        subprocess.run(["docker", "rm", "--force", container], check=True, capture_output=True)


if __name__ == "__main__":
    raise SystemExit(main())
