"""Compare an exported converted ComWorld sun with a shipped Replay ComWorld."""

import argparse
import json
from pathlib import Path


def fields(path):
    return json.loads(path.read_text(encoding="utf-8"))["asset"]["fields"]


def compare(reference, candidate, reference_index=1, candidate_index=1):
    expected = fields(reference)["primaryLights"]["values"][reference_index]
    actual = fields(candidate)["primaryLights"]["values"][candidate_index]
    # Entity IDs belong to their source map and are not lighting parameters.
    keys = set(expected) - {"entityId"}
    mismatches = {key: {"expected": expected[key], "actual": actual.get(key)}
                  for key in sorted(keys) if actual.get(key) != expected[key]}
    if mismatches:
        raise ValueError(json.dumps(mismatches, indent=2))
    return {"reference": str(reference), "candidate": str(candidate),
            "matched_fields": len(keys), "intensity": actual["intensity"],
            "success": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, nargs="+", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = [compare(args.reference, candidate) for candidate in args.candidate]
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
