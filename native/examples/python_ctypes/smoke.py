"""Smoke test for the installed Number X-Ray ctypes loader."""

from __future__ import annotations

import json
import pathlib
import sys
from typing import List

from number_xray_ctypes import NumberXRayLoadError, load


def main(argv: List[str]) -> int:
    if len(argv) != 2:
        print("usage: smoke.py <number-xray-install-prefix>", file=sys.stderr)
        return 2

    prefix = pathlib.Path(argv[1]).resolve()
    try:
        library = load(prefix)
    except NumberXRayLoadError as exc:
        print(f"load failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    ok = ok and bool(library.version())
    ok = ok and library.abi_version() >= 1
    backend = library.backend_info()
    ok = ok and bool(backend["name"]) and bool(backend["version"]) and bool(backend["library"])
    route = library.bigint_route_config()
    ok = ok and route["wordBits"] == 64
    ok = ok and route["karatsubaThresholdLimbs"] > 0
    ok = ok and route["decimalHornerMinLimbs"] > 0
    ok = ok and route["mulUnroll4RouteMinLimbs"] <= route["mulUnroll4RouteMaxLimbs"]
    if route["mulUnroll4RouteEnabled"]:
        ok = ok and route["msvcUint128Helpers"]
    ok = ok and library.add_decimal("10,000_000 000,000_000 000", "1") == "10000000000000000001"
    ok = ok and library.sub_decimal("1000", "1") == "999"
    ok = ok and library.mul_decimal("12345", "6789") == "83810205"
    ok = ok and library.square_decimal("4 294 967 296") == "18446744073709551616"
    ok = ok and library.compare_decimal("001_000", "1000") == 0
    ok = ok and library.compare_decimal("999", "1000") == -1
    ok = ok and library.compare_decimal("1001", "1000") == 1
    ok = ok and library.add_decimal("12x", "1") is None
    ok = ok and library.sub_decimal("1", "2") is None

    factor_text = library.factor_solve_json("10_403")
    ok = ok and factor_text is not None
    factor = library.factor_solve("10_403") or {}
    if factor_text:
        ok = ok and json.loads(factor_text).get("factorReport") is not None
    else:
        ok = False
    ok = ok and factor.get("status") == "solved"
    ok = ok and factor.get("productVerified") is True

    if ok:
        print(
            "Number X-Ray ctypes import ok: "
            f"{library.version()} / {backend['name']} {backend['version']}"
        )
        return 0

    print("Number X-Ray ctypes import smoke failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
