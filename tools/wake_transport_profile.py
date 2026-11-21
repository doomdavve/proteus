#!/usr/bin/env python3
"""Aggregate Instruments time-profile XML exported for the private CPU evaluator."""
import argparse
from collections import Counter
import hashlib
import json
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    args = parser.parse_args()
    root = ET.parse(args.input).getroot()
    ids = {e.get("id"): e for e in root.iter() if e.get("id")}

    def resolve(element):
        return ids[element.get("ref")] if element.get("ref") else element

    leaf, inclusive = Counter(), Counter()
    total = samples = 0
    for row in root.iter("row"):
        columns = list(row)
        if len(columns) != 7 or resolve(columns[4]).get("fmt") != "Running":
            continue
        weight = int(resolve(columns[5]).text or 0)
        stack = resolve(columns[6])
        frames = [resolve(e).get("name", "?") for e in resolve(list(stack)[0])]
        if not frames:
            continue
        total += weight
        samples += 1
        leaf[frames[0]] += weight
        for name in set(frames):
            inclusive[name] += weight
    assert samples > 0 and total > 0

    def table(counter):
        return [dict(name=name, ms=value/1e6, percent=100*value/total)
                for name, value in counter.most_common(20)]

    with open(args.input, "rb") as stream:
        digest = hashlib.sha256(stream.read()).hexdigest()
    with open(args.output, "w") as stream:
        json.dump(dict(samples=samples, running_weight_ms=total/1e6, leaf=table(leaf),
                       inclusive=table(inclusive), input_sha256=digest), stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
