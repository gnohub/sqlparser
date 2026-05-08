#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Verify sqlparser CLI batch output against the fixture.")
    parser.add_argument("--fixture", required=True, help="Input fixture JSON.")
    parser.add_argument("--output", required=True, help="CLI output JSON.")
    return parser.parse_args()


def load_json(path_text):
    path = Path(path_text)
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def expect(condition, message):
    if not condition:
        raise SystemExit(message)


def expected_success(item):
    expect_root = item.get("expect", {})
    return expect_root.get("ok", True) is not False


def verify_view(view, case_name):
    expect(isinstance(view, dict), "case {} missing view object".format(case_name))
    expect(isinstance(view.get("statements"), list), "case {} view statements missing".format(case_name))


def main():
    args = parse_args()
    fixture_root = load_json(args.fixture)
    output_root = load_json(args.output)

    fixture_items = fixture_root.get("items")
    output_items = output_root.get("items")

    expect(isinstance(fixture_items, list), "fixture does not contain an items array")
    expect(isinstance(output_items, list), "output does not contain an items array")
    expect(len(output_items) == len(fixture_items), "output item count does not match fixture")

    expected_total = len(fixture_items)
    expected_succeeded = sum(1 for item in fixture_items if expected_success(item))
    expected_failed = expected_total - expected_succeeded

    expect(output_root.get("total") == expected_total, "output total mismatch")
    expect(output_root.get("succeeded") == expected_succeeded, "output succeeded mismatch")
    expect(output_root.get("failed") == expected_failed, "output failed mismatch")

    expected_names = {item["name"] for item in fixture_items}
    actual_names = set()

    for fixture_item, output_item in zip(fixture_items, output_items):
        case_name = fixture_item["name"]
        actual_names.add(output_item.get("name"))
        expect(output_item.get("name") == case_name, "output order mismatch for case {}".format(case_name))

        if expected_success(fixture_item):
            expect(output_item.get("ok") is True, "case {} should succeed".format(case_name))
            verify_view(output_item.get("view"), case_name)
        else:
            expect(output_item.get("ok") is False, "case {} should fail".format(case_name))
            error = output_item.get("error")
            expect(isinstance(error, dict), "case {} missing error payload".format(case_name))
            expect(isinstance(error.get("message"), str) and error["message"], "case {} missing error message".format(case_name))

    expect(actual_names == expected_names, "output case names do not match fixture")


if __name__ == "__main__":
    main()
