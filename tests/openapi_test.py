"""Run with: python3 tests/openapi_test.py build/movie-theater-booking"""

import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
SPEC_FILE = Path(__file__).resolve().parents[1] / "docs/openapi.json"


class OpenApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="booking-openapi-")
        cls.addClassCleanup(cls.directory.cleanup)
        config = {
            "movies": [{"id": 1, "name": "Test Movie"}],
            "theaters": [{"id": 10, "name": "Test Theater"}],
            "screenings": [
                {"id": i, "movieId": 1, "theaterId": 10,
                 "seats": [{"name": "A1"}, {"name": "A2"}]}
                for i in (100, 101)
            ],
        }
        config_path = Path(cls.directory.name) / "db.json"
        config_path.write_text(json.dumps(config))
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            cls.port = sock.getsockname()[1]
        cls.log = open(Path(cls.directory.name) / "server.log", "w+")
        cls.addClassCleanup(cls.log.close)
        cls.server = subprocess.Popen(
            [str(BINARY), "--address", "127.0.0.1", "--port", str(cls.port),
             "--log-level", "error"],
            cwd=cls.directory.name,
            env=dict(os.environ, CONFIG_DB_FILE_PATH=str(config_path)),
            stdout=cls.log, stderr=subprocess.STDOUT,
        )
        cls.addClassCleanup(cls.stop_server)
        for _ in range(100):
            try:
                if cls.request("GET", "/openapi.json")[0] == 200:
                    return
            except OSError:
                pass
            if cls.server.poll() is not None:
                break
            time.sleep(0.05)
        cls.log.seek(0)
        raise RuntimeError("Test server did not start: " + cls.log.read())

    @classmethod
    def stop_server(cls):
        cls.server.terminate()
        try:
            cls.server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            cls.server.kill()
            cls.server.wait()

    @classmethod
    def request(cls, method, path, body=None):
        connection = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=5)
        try:
            connection.request(method, path, None if body is None else json.dumps(body),
                               {"Content-Type": "application/json"})
            response = connection.getresponse()
            content_type = response.getheader("Content-Type", "")
            raw = response.read().decode()
            return response.status, content_type, json.loads(raw) if "application/json" in content_type else raw
        finally:
            connection.close()

    def test_spec_is_embedded_and_references_resolve(self):
        status, content_type, spec = self.request("GET", "/openapi.json")
        self.assertEqual(status, 200)
        self.assertIn("application/json", content_type)
        self.assertEqual(spec, json.loads(SPEC_FILE.read_text()))
        self.assertEqual(spec["openapi"], "3.0.3")
        self.assertEqual(spec["servers"][0]["url"], "/")
        ids = []
        for path in spec["paths"].values():
            for operation in path.values():
                ids.append(operation["operationId"])
        self.assertEqual(len(ids), len(set(ids)))

        def walk(value):
            if isinstance(value, dict):
                if "$ref" in value:
                    ref = value["$ref"]
                    self.assertTrue(ref.startswith("#/"))
                    target = spec
                    for part in ref[2:].split("/"):
                        target = target[part]
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(spec)

    def test_swagger_ui_served_from_any_working_directory(self):
        for path in ("/docs", "/docs/"):
            status, content_type, html = self.request("GET", path)
            self.assertEqual(status, 200)
            self.assertIn("text/html", content_type)
            self.assertIn('url: "/openapi.json"', html)
            self.assertIn("swagger-ui-dist@5.17.14", html)
            self.assertIn("validatorUrl: null", html)

    def test_documented_get_endpoints(self):
        cases = [
            ("/movies", [{"id": 1, "movie": "Test Movie"}]),
            ("/theaters", [{"id": 10, "name": "Test Theater"}]),
            ("/theater?movieId=1", [{"id": 10, "name": "Test Theater"}]),
            ("/theater?movieId=999", []),
            ("/available_seats?screeningId=100",
             [{"seat": "A1", "isBooked": False}, {"seat": "A2", "isBooked": False}]),
            ("/available_seats?screeningId=999", []),
        ]
        for url, expected in cases:
            with self.subTest(url=url):
                status, _, body = self.request("GET", url)
                self.assertEqual(status, 200)
                self.assertEqual(body, expected)

    def test_screenings_supports_unfiltered_and_filtered_requests(self):
        status, _, body = self.request("GET", "/screenings")
        self.assertEqual(status, 200)
        self.assertEqual({s["id"] for s in body}, {100, 101})
        self.assertTrue(all({"movieId", "theaterId", "seats"} <= s.keys() for s in body))
        status, _, body = self.request("GET", "/screenings?movieId=1&theaterId=10")
        self.assertEqual(status, 200)
        self.assertEqual(sorted(body, key=lambda s: s["id"]), [{"id": 100}, {"id": 101}])
        self.assertEqual(self.request("GET", "/screenings?movieId=999&theaterId=10")[2], [])

    def test_documented_bad_request_responses(self):
        for url in ("/screenings?movieId=1", "/screenings?theaterId=10",
                    "/screenings?movieId=no&theaterId=10", "/screenings?movieId=1&theaterId=4294967296",
                    "/theater", "/theater?movieId=-1", "/available_seats", "/available_seats?screeningId=no"):
            with self.subTest(url=url):
                status, _, body = self.request("GET", url)
                self.assertEqual(status, 400)
                self.assertIsInstance(body["error"], str)

    def test_documented_booking_responses_and_persistence(self):
        self.assertEqual(self.request("POST", "/bookings", {})[0], 400)
        payload = {"screeningId": 101, "seats": ["A1"]}
        status, _, body = self.request("POST", "/bookings", payload)
        self.assertEqual(status, 200)
        self.assertEqual(body, {"booked": True, **payload})
        status, _, body = self.request("POST", "/bookings", payload)
        self.assertEqual(status, 409)
        self.assertIn("error", body)
        self.assertEqual(self.request("GET", "/available_seats?screeningId=101")[2],
                         [{"seat": "A2", "isBooked": False}])


if __name__ == "__main__":
    unittest.main()
