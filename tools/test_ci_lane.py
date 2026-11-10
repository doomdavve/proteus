#!/usr/bin/env python3
"""The PR lane must stay bounded and retain its essential checks."""
import unittest
from check_ci_lane import REQUIRED, check


def lane(count=50):
    names = sorted(REQUIRED) + [f"unit_{i}" for i in range(count - len(REQUIRED))]
    return [{"name": name, "properties": [{"name": "LABELS", "value": ["ci", "quick"]}]}
            for name in names]


class Budget(unittest.TestCase):
    def test_supported_lane(self):
        check(lane())
        check(lane(150))

    def test_count_limits(self):
        for count in (49, 151):
            with self.subTest(count=count), self.assertRaises(ValueError):
                check(lane(count))

    def test_missing_gate(self):
        tests = lane()
        tests[0]["name"] = "unimportant_replacement"
        with self.assertRaisesRegex(ValueError, "critical"):
            check(tests)

    def test_expensive_or_disabled(self):
        for extra in ({"name": "DISABLED", "value": True},
                      {"name": "LABELS", "value": ["ci", "gpu"]},
                      {"name": "LABELS", "value": ["ci", "slow"]}):
            tests = lane()
            tests[-1]["properties"].append(extra)
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                check(tests)


if __name__ == "__main__":
    unittest.main()
