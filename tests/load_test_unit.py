"""Checks for load generation and result validation; no Docker or sockets required."""

import asyncio
from collections import Counter
import json
import gzip
from types import SimpleNamespace
import unittest

from load_test import Oracle, Result, distribution, fixture, operation, run_load, summarize, validate_responses


class LoadTestUnit(unittest.TestCase):
    def test_default_mix_and_unique_booking_targets(self):
        data = fixture(30000)
        screenings = {s['id']: s for s in data['screenings']}
        self.assertEqual(len(screenings), len(data['screenings']))
        methods, targets = Counter(), set()
        for i in range(30000):
            method, _, payload = operation(i)
            methods[method] += 1
            if payload:
                target = payload['screeningId'], payload['seats'][0]
                self.assertNotIn(target, targets)
                targets.add(target)
                self.assertIn({'name': target[1]}, screenings[target[0]]['seats'])
        self.assertEqual(methods, {'GET': 27000, 'POST': 3000})
        self.assertEqual(len(targets), 3000)

    def test_oracle_rejects_missing_duplicate_and_incorrect_records(self):
        oracle = Oracle(fixture(100))
        movies = list(oracle.movies.values())
        oracle.check(0, movies[::-1])  # Map ordering is intentionally unspecified.
        for body in (movies[:-1], movies + movies[:1], [{'id': 1, 'movie': 'wrong'}], {}):
            with self.subTest(body=body), self.assertRaises(ValueError):
                oracle.check(0, body)
        request = operation(9)[2]
        oracle.check(9, {'booked': True, **request})
        with self.assertRaises(ValueError):
            oracle.check(9, {'booked': True, 'screeningId': 999, 'seats': request['seats']})

    def test_oracle_requires_summary_fields_and_rejects_seat_payloads(self):
        data = fixture(100)
        oracle = Oracle(data)
        records = [{key: row[key] for key in ("id", "movieId", "theaterId")}
                   for row in data['screenings']]
        oracle.check(8, records)
        records[0]['seats'] = []
        with self.assertRaises(ValueError):
            oracle.check(8, records)
        del records[0]['seats']
        records[0]['movieId'] = 999
        with self.assertRaises(ValueError):
            oracle.check(8, records)

    def test_latency_distribution_and_failure_accounting(self):
        self.assertIsNone(distribution([])['mean'])
        self.assertEqual(distribution([10, 20, 30])['mean'], 20)
        self.assertEqual(distribution([10, 20, 30])['p99'], 30)
        records = [Result(0, 'GET', '/movies', 200, 2, 0, 20, 'ok'),
                   Result(1, 'GET', '/theaters', 200, 4, 0, 20, 'wrong_body'),
                   Result(9, 'POST', '/bookings', None, 1000, 0, 0, 'timeout')]
        args = SimpleNamespace(rate=10, duration=1, timeout=1, max_lag=.1, max_in_flight=2, small_gets_only=False)
        report = summarize(records, Counter(in_flight_limit=7), 2, 1, 2, {}, [], .1, 0, args)
        self.assertEqual(report['observed']['responded'], 2)
        self.assertEqual(report['observed']['no_complete_response'], 1)
        self.assertEqual(report['observed']['wrong_answers'], 1)
        self.assertEqual(report['observed']['not_sent'], 7)
        self.assertEqual(report['overall']['response_latency_ms']['mean'], 3)

    def test_offline_validation_checks_gzip_json_and_wrong_answers(self):
        oracle = Oracle(fixture(100))
        good = Result(9, 'POST', '/bookings', 200, 1, 0, 0, 'ok')
        bad = Result(19, 'POST', '/bookings', 200, 1, 0, 0, 'ok')
        invalid = Result(0, 'GET', '/movies', 200, 1, 0, 0, 'ok')
        body = json.dumps({'booked': True, **operation(9)[2]}).encode()
        validate_responses([(good, gzip.compress(body), 'gzip'),
                            (bad, b'{}', ''), (invalid, b'not json', '')], oracle)
        self.assertEqual(good.outcome, 'ok')
        self.assertEqual(good.bytes_received, len(body))
        self.assertEqual(bad.outcome, 'wrong_body')
        self.assertEqual(invalid.outcome, 'invalid_json')


class PacingTest(unittest.IsolatedAsyncioTestCase):
    async def test_slow_server_does_not_reduce_offered_rate(self):
        class Response:
            status = 200
            headers = {}

            async def __aenter__(self):
                await asyncio.sleep(.2)
                return self

            async def __aexit__(self, *args):
                pass

            async def read(self):
                return json.dumps([]).encode()

        class Session:
            def request(self, *args, **kwargs):
                return Response()

        args = SimpleNamespace(rate=20, duration=1, max_in_flight=2, max_lag=.1)
        records, unsent, elapsed, _, peak, _ = await run_load(Session(), '', args)
        self.assertEqual(len(records) + sum(unsent.values()), 20)
        self.assertGreater(unsent['in_flight_limit'], 0)
        self.assertLess(elapsed, 1.5)
        self.assertLessEqual(peak, 2)


if __name__ == '__main__':
    unittest.main()
