"""Small ctypes loader for an installed Number X-Ray SDK.

This module intentionally uses only the Python standard library. Point it at an
SDK install prefix and it reads ``share/number-xray/number-xray-sdk.json`` to
locate the shared library and ownership contract.
"""

from __future__ import annotations

import ctypes
import json
import os
import pathlib
import sys
from typing import Any, Dict, Optional, Tuple, Union


class NumberXRayLoadError(RuntimeError):
    """Raised when the installed SDK manifest or shared library cannot load."""


class XrayBigIntRouteConfig(ctypes.Structure):
    """ctypes mirror of the C ``XrayBigIntRouteConfig`` value type."""

    _fields_ = [
        ("word_bits", ctypes.c_uint),
        ("karatsuba_threshold_limbs", ctypes.c_size_t),
        ("decimal_horner_min_limbs", ctypes.c_size_t),
        ("mul_unroll4_route_min_limbs", ctypes.c_size_t),
        ("mul_unroll4_route_max_limbs", ctypes.c_size_t),
        ("mul_unroll4_route_enabled", ctypes.c_int),
        ("msvc_uint128_helpers", ctypes.c_int),
    ]


def _encode(text: str) -> bytes:
    return text.encode("utf-8")


class NumberXRayLibrary:
    """Thin ctypes facade over the shared Number X-Ray C API."""

    def __init__(
        self,
        prefix: Optional[Union[os.PathLike, str]] = None,
        manifest_path: Optional[Union[os.PathLike, str]] = None,
    ) -> None:
        self.prefix, self.manifest_path = self._resolve_paths(prefix, manifest_path)
        self.manifest = self._read_manifest(self.manifest_path)
        self._dll_directory_handles = []
        self.library_path = self._find_shared_library()
        self._prepare_dll_search()
        self._lib = ctypes.CDLL(str(self.library_path))
        self._configure_prototypes()

    @staticmethod
    def _resolve_paths(
        prefix: Optional[Union[os.PathLike, str]],
        manifest_path: Optional[Union[os.PathLike, str]],
    ) -> Tuple[pathlib.Path, pathlib.Path]:
        if manifest_path is not None:
            manifest = pathlib.Path(manifest_path).resolve()
            try:
                prefix_path = manifest.parents[2]
            except IndexError as exc:
                raise NumberXRayLoadError(f"Cannot infer prefix from manifest: {manifest}") from exc
            return prefix_path, manifest

        if prefix is None:
            prefix_path = pathlib.Path.cwd()
        else:
            prefix_path = pathlib.Path(prefix).resolve()
        manifest = prefix_path / "share" / "number-xray" / "number-xray-sdk.json"
        return prefix_path, manifest

    @staticmethod
    def _read_manifest(path: pathlib.Path) -> Dict[str, Any]:
        if not path.exists():
            raise NumberXRayLoadError(f"SDK manifest not found: {path}")
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)

    def _find_shared_library(self) -> pathlib.Path:
        libraries = self.manifest.get("libraries", {})
        shared = libraries.get("coreShared")
        if not isinstance(shared, dict):
            raise NumberXRayLoadError("SDK manifest does not advertise a shared core library")

        name = str(shared.get("name", "number_xray"))
        lib_dir = self.prefix / str(shared.get("libDir", "lib"))
        bin_dir = self.prefix / str(shared.get("binDir", "bin"))

        if sys.platform == "win32":
            candidates = [
                bin_dir / f"{name}.dll",
                lib_dir / f"{name}.dll",
            ]
        elif sys.platform == "darwin":
            candidates = [
                lib_dir / f"lib{name}.dylib",
                lib_dir / f"{name}.dylib",
            ]
        else:
            candidates = [
                lib_dir / f"lib{name}.so",
                lib_dir / f"{name}.so",
            ]

        for candidate in candidates:
            if candidate.exists():
                return candidate
        joined = ", ".join(str(candidate) for candidate in candidates)
        raise NumberXRayLoadError(f"Shared library not found. Tried: {joined}")

    def _prepare_dll_search(self) -> None:
        directories = [self.prefix / "bin"]
        extra = os.environ.get("NUMBER_XRAY_EXTRA_DLL_DIRS", "")
        for item in extra.split(os.pathsep):
            if item:
                directories.append(pathlib.Path(item))

        existing = []
        for directory in directories:
            if directory.exists():
                existing.append(str(directory.resolve()))

        if existing:
            os.environ["PATH"] = os.pathsep.join(existing + [os.environ.get("PATH", "")])

        add_dll_directory = getattr(os, "add_dll_directory", None)
        if add_dll_directory is not None and sys.platform == "win32":
            for directory in existing:
                self._dll_directory_handles.append(add_dll_directory(directory))

    def _configure_prototypes(self) -> None:
        self._lib.xray_version.argtypes = []
        self._lib.xray_version.restype = ctypes.c_char_p
        self._lib.xray_abi_version.argtypes = []
        self._lib.xray_abi_version.restype = ctypes.c_uint
        self._lib.xray_bignum_backend_name.argtypes = []
        self._lib.xray_bignum_backend_name.restype = ctypes.c_char_p
        self._lib.xray_bignum_backend_version.argtypes = []
        self._lib.xray_bignum_backend_version.restype = ctypes.c_char_p
        self._lib.xray_bignum_backend_library.argtypes = []
        self._lib.xray_bignum_backend_library.restype = ctypes.c_char_p
        self._lib.xray_bigint_route_config.argtypes = []
        self._lib.xray_bigint_route_config.restype = XrayBigIntRouteConfig
        self._lib.xray_bigint_route_config_json.argtypes = []
        self._lib.xray_bigint_route_config_json.restype = ctypes.c_void_p
        self._lib.xray_free.argtypes = [ctypes.c_void_p]
        self._lib.xray_free.restype = None

        for name in (
            "xray_bigint_add_decimal",
            "xray_bigint_sub_decimal",
            "xray_bigint_mul_decimal",
        ):
            function = getattr(self._lib, name)
            function.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            function.restype = ctypes.c_void_p

        self._lib.xray_bigint_square_decimal.argtypes = [ctypes.c_char_p]
        self._lib.xray_bigint_square_decimal.restype = ctypes.c_void_p
        self._lib.xray_bigint_compare_decimal.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int),
        ]
        self._lib.xray_bigint_compare_decimal.restype = ctypes.c_int

        for name in (
            "xray_factor_solve_json",
            "xray_cyclotomic_scan_json",
            "xray_workbench_run_json",
        ):
            function = getattr(self._lib, name)
            function.argtypes = [ctypes.c_char_p]
            function.restype = ctypes.c_void_p

    @staticmethod
    def _borrowed_string(value: bytes) -> str:
        return value.decode("utf-8")

    def _owned_string(self, function: Any, *arguments: str) -> Optional[str]:
        encoded = [_encode(argument) for argument in arguments]
        pointer = function(*encoded)
        if not pointer:
            return None
        try:
            return ctypes.string_at(pointer).decode("utf-8")
        finally:
            self._lib.xray_free(pointer)

    def version(self) -> str:
        return self._borrowed_string(self._lib.xray_version())

    def abi_version(self) -> int:
        return int(self._lib.xray_abi_version())

    def backend_info(self) -> Dict[str, str]:
        return {
            "name": self._borrowed_string(self._lib.xray_bignum_backend_name()),
            "version": self._borrowed_string(self._lib.xray_bignum_backend_version()),
            "library": self._borrowed_string(self._lib.xray_bignum_backend_library()),
        }

    def bigint_route_config(self) -> Dict[str, Union[int, bool]]:
        route = self._lib.xray_bigint_route_config()
        return {
            "wordBits": int(route.word_bits),
            "karatsubaThresholdLimbs": int(route.karatsuba_threshold_limbs),
            "decimalHornerMinLimbs": int(route.decimal_horner_min_limbs),
            "mulUnroll4RouteMinLimbs": int(route.mul_unroll4_route_min_limbs),
            "mulUnroll4RouteMaxLimbs": int(route.mul_unroll4_route_max_limbs),
            "mulUnroll4RouteEnabled": bool(route.mul_unroll4_route_enabled),
            "msvcUint128Helpers": bool(route.msvc_uint128_helpers),
        }

    def bigint_route_summary_json(self) -> Optional[str]:
        """Return the full scratch bigint route map as a JSON string."""

        return self._owned_string(self._lib.xray_bigint_route_config_json)

    def bigint_route_summary(self) -> Optional[Dict[str, Any]]:
        """Return the full scratch bigint route map as a Python dictionary."""

        text = self.bigint_route_summary_json()
        return json.loads(text) if text is not None else None

    def add_decimal(self, left: str, right: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_bigint_add_decimal, left, right)

    def sub_decimal(self, left: str, right: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_bigint_sub_decimal, left, right)

    def mul_decimal(self, left: str, right: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_bigint_mul_decimal, left, right)

    def square_decimal(self, value: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_bigint_square_decimal, value)

    def compare_decimal(self, left: str, right: str) -> Optional[int]:
        comparison = ctypes.c_int()
        ok = self._lib.xray_bigint_compare_decimal(
            _encode(left), _encode(right), ctypes.byref(comparison)
        )
        if not ok:
            return None
        return int(comparison.value)

    def factor_solve_json(self, value: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_factor_solve_json, value)

    def factor_solve(self, value: str) -> Optional[Dict[str, Any]]:
        text = self.factor_solve_json(value)
        if text is None:
            return None
        payload = json.loads(text)
        report = payload.get("factorReport")
        return report if isinstance(report, dict) else payload

    def cyclotomic_scan_json(self, value: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_cyclotomic_scan_json, value)

    def workbench_run_json(self, value: str) -> Optional[str]:
        return self._owned_string(self._lib.xray_workbench_run_json, value)


def load(
    prefix: Optional[Union[os.PathLike, str]] = None,
    manifest_path: Optional[Union[os.PathLike, str]] = None,
) -> NumberXRayLibrary:
    """Load Number X-Ray from an installed SDK prefix or manifest path."""

    return NumberXRayLibrary(prefix=prefix, manifest_path=manifest_path)


if __name__ == "__main__":
    sdk_prefix = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else pathlib.Path.cwd()
    library = load(sdk_prefix)
    print(f"Number X-Ray {library.version()} ABI {library.abi_version()}")
    backend = library.backend_info()
    print(f"Backend: {backend['name']} {backend['version']} ({backend['library']})")
    route = library.bigint_route_config()
    route_summary = library.bigint_route_summary() or route
    print(
        "Bigint route: "
        f"word={route['wordBits']}b "
        f"karatsuba>={route['karatsubaThresholdLimbs']} limbs "
        f"dcWideChunks>={route_summary.get('decimalDcMinWideChunks', 'unknown')}"
    )
    print(f"10,403 + 1 = {library.add_decimal('10,403', '1')}")
