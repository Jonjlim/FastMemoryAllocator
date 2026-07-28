#!/usr/bin/env python3
"""
Run the mimalloc-bench suite against cmalloc and the reference allocators.

Every benchmark is compiled once per allocator. C benchmarks are redirected
with a force-included shim header; C++ benchmarks get replacement operator new
and operator delete linked in. All four allocators are linked as dynamic
libraries so the call structure is identical across backends.

Usage:
    python3 bench/run_bench.py --list
    python3 bench/run_bench.py --tests cfrac,espresso --reps 3
    python3 bench/run_bench.py --allocs sys,cm,mi --procs 8
"""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = ROOT / "bench"
SHIM_DIR = BENCH_DIR / "shim"
SUITE = ROOT / "extern" / "mimalloc-bench" / "bench"
BUILD = BENCH_DIR / "build"
RESULTS = BENCH_DIR / "results"

BREW = subprocess.run(
    ["brew", "--prefix"], capture_output=True, text=True
).stdout.strip() or "/opt/homebrew"

CFRAC_SRCS = """cfrac.c pops.c pconst.c pio.c pabs.c pneg.c pcmp.c podd.c phalf.c
padd.c psub.c pmul.c pdivmod.c psqrt.c ppowmod.c atop.c ptoa.c itop.c utop.c
ptou.c errorp.c pfloat.c pidiv.c pimod.c picmp.c primes.c pcfrac.c pgcd.c""".split()

ESPRESSO_SRCS = """cofactor.c cols.c compl.c contain.c cubestr.c cvrin.c cvrm.c
cvrmisc.c cvrout.c dominate.c equiv.c espresso.c essen.c exact.c expand.c gasp.c
getopt.c gimpel.c globals.c hack.c indep.c irred.c main.c map.c matrix.c mincov.c
opo.c pair.c part.c primes.c reduce.c rows.c set.c setc.c sharp.c sminterf.c
solution.c sparse.c unate.c utility.c verify.c""".split()

BARNES_SRCS = "code.c code_io.c load.c grav.c getparam.c util.c".split()


def srcs(subdir, names):
    return [f"{subdir}/{n}" for n in names]


# lang: "c" uses the force-included redirect header.
#       "cpp" relies on replaced operator new / operator delete.
# metric: how a run is scored.
#       "wall"       -> elapsed wall-clock seconds (fixed work)
#       "regex"      -> a relative time printed by the benchmark
#       "ops"/"iters"-> throughput converted to a relative time
BENCHMARKS = {
    "cfrac": dict(
        lang="c", srcs=srcs("cfrac", CFRAC_SRCS), defines=["NOMEMOPT=1"],
        libs=["m"], args=["17545186520507317056371138836327483792789528"],
        metric="wall", threads=False,
    ),
    "espresso": dict(
        lang="c", srcs=srcs("espresso", ESPRESSO_SRCS), libs=["m"],
        args=[str(SUITE / "espresso" / "largest.espresso")],
        metric="wall", threads=False,
    ),
    "barnes": dict(
        lang="c", srcs=srcs("barnes", BARNES_SRCS), libs=["m"],
        stdin=str(SUITE / "barnes" / "input"), metric="wall", threads=False,
    ),
    "alloc-test1": dict(
        lang="cpp", srcs=["alloc-test/test_common.cpp", "alloc-test/allocator_tester.cpp"],
        defines=["BENCH=4"], binary="alloc-test", args=["1"],
        metric="wall", threads=False,
    ),
    "alloc-testN": dict(
        lang="cpp", srcs=["alloc-test/test_common.cpp", "alloc-test/allocator_tester.cpp"],
        defines=["BENCH=4"], binary="alloc-test", args=["@PROCS16"],
        metric="wall", threads=True,
    ),
    "larsonN": dict(
        lang="cpp", srcs=["larson/larson.cpp"], defines=["CPP=1"],
        binary="larson", args=["5", "8", "1000", "5000", "100", "4141", "@PROCS"],
        metric="regex", regex=r"relative time:\s*([0-9.]+)s", threads=True,
    ),
    "larsonN-sized": dict(
        lang="cpp", srcs=["larson/larson.cpp"], defines=["CPP=1", "SIZED=1"],
        cxxflags=["-fsized-deallocation"], binary="larson-sized",
        args=["5", "8", "1000", "5000", "100", "4141", "@PROCS"],
        metric="regex", regex=r"relative time:\s*([0-9.]+)s", threads=True,
    ),
    "xmalloc-testN": dict(
        lang="c", srcs=["xmalloc-test/xmalloc-test.c"], binary="xmalloc-test",
        args=["-w", "@PROCS", "-t", "5", "-s", "64"],
        metric="regex", regex=r"rtime:\s*([0-9.]+)", threads=True,
    ),
    "cache-scratch1": dict(
        lang="cpp", srcs=["cache-scratch/cache-scratch.cpp"], binary="cache-scratch",
        args=["1", "1000", "1", "2000000", "@PROCS"], metric="wall", threads=False,
    ),
    "cache-scratchN": dict(
        lang="cpp", srcs=["cache-scratch/cache-scratch.cpp"], binary="cache-scratch",
        args=["@PROCS", "1000", "1", "2000000", "@PROCS"], metric="wall", threads=True,
    ),
    "cache-thrash1": dict(
        lang="cpp", srcs=["cache-thrash/cache-thrash.cpp"], binary="cache-thrash",
        args=["1", "1000", "1", "2000000", "@PROCS"], metric="wall", threads=False,
    ),
    "cache-thrashN": dict(
        lang="cpp", srcs=["cache-thrash/cache-thrash.cpp"], binary="cache-thrash",
        args=["@PROCS", "1000", "1", "2000000", "@PROCS"], metric="wall", threads=True,
    ),
    "malloc-large": dict(
        lang="cpp", srcs=["malloc-large/malloc-large.cpp"], metric="wall", threads=False,
    ),
    "mstressN": dict(
        lang="c", srcs=["mstress/mstress.c"], binary="mstress",
        args=["@PROCS", "50", "25"], metric="wall", threads=True,
    ),
    "mleak": dict(
        lang="c", srcs=["mleak/mleak.c"], args=["5"], metric="wall", threads=False,
    ),
    "rptestN": dict(
        lang="c", srcs=["rptest/rptest.c", "rptest/thread.c", "rptest/timer.c"],
        includes=["rptest"], binary="rptest",
        args=["@PROCS", "0", "1", "2", "500", "1000", "100", "8", "16000"],
        metric="ops", regex=r"\.\.\.(\d+) memory ops", ops_base=2000000.0, threads=True,
    ),
    "glibc-simple": dict(
        lang="c", srcs=["glibc-bench/bench-malloc-simple.c"], metric="wall", threads=False,
    ),
    "glibc-thread": dict(
        lang="c", srcs=["glibc-bench/bench-malloc-thread.c"], args=["@PROCS"],
        metric="ops", regex=r"(\d+) iterations", ops_base=1e9, threads=True,
    ),
}

ALLOCATORS = {
    "sys": dict(name="system", define="SHIM_BACKEND_SYS", cflags=[], ldflags=[]),
    "cm": dict(name="cmalloc", define="SHIM_BACKEND_CM",
               cflags=[f"-I{ROOT/'include'}", f"-I{ROOT/'src'}"],
               ldflags=[f"-L{BUILD/'lib'}", "-lcmalloc",
                        f"-Wl,-rpath,{BUILD/'lib'}"]),
    "mi": dict(name="mimalloc", define="SHIM_BACKEND_MI",
               cflags=[f"-I{BREW}/include"],
               ldflags=[f"-L{BREW}/lib", "-lmimalloc", f"-Wl,-rpath,{BREW}/lib"]),
    "je": dict(name="jemalloc", define="SHIM_BACKEND_JE",
               cflags=[f"-I{BREW}/include"],
               ldflags=[f"-L{BREW}/lib", "-ljemalloc", f"-Wl,-rpath,{BREW}/lib"]),
    "tc": dict(name="tcmalloc", define="SHIM_BACKEND_TC",
               cflags=[f"-I{BREW}/include"],
               ldflags=[f"-L{BREW}/lib", "-ltcmalloc_minimal",
                        f"-Wl,-rpath,{BREW}/lib"]),
}

BASE_CFLAGS = [
    "-O2", "-w", "-fno-builtin-malloc", "-fno-builtin-calloc",
    "-fno-builtin-realloc", "-fno-builtin-free",
    "-Wno-implicit-function-declaration", "-Wno-implicit-int",
    "-Wno-int-conversion", "-Wno-return-mismatch",
    "-Wno-incompatible-pointer-types",
]


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def build_cmalloc_dylib(verbose):
    """Build libcmalloc as a dylib so it is linked the same way as mi/je/tc.

    Kept under bench/build so it never shadows the static lib/libcmalloc.a that
    the regular test targets link against.
    """
    libdir = BUILD / "lib"
    libdir.mkdir(parents=True, exist_ok=True)
    objdir = BUILD / "cmalloc-obj"
    objdir.mkdir(parents=True, exist_ok=True)

    objs = []
    for src in sorted((ROOT / "src").glob("*.c")):
        obj = objdir / (src.stem + ".o")
        cmd = ["clang", "-O3", "-flto", "-fPIC", f"-I{ROOT/'include'}",
               "-c", str(src), "-o", str(obj)]
        r = run(cmd)
        if r.returncode != 0:
            print(r.stderr, file=sys.stderr)
            raise SystemExit(f"failed to compile {src}")
        objs.append(str(obj))

    out = libdir / "libcmalloc.dylib"
    cmd = ["clang", "-dynamiclib", "-install_name", f"@rpath/{out.name}",
           "-o", str(out)] + objs
    r = run(cmd)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        raise SystemExit("failed to link libcmalloc.dylib")
    if verbose:
        print(f"  built {out}")


def build_shim(alloc_key, verbose):
    """Compile the C shim and the C++ new/delete replacement for one backend."""
    a = ALLOCATORS[alloc_key]
    outdir = BUILD / alloc_key
    outdir.mkdir(parents=True, exist_ok=True)

    c_obj = outdir / "alloc_shim.o"
    cmd = (["clang"] + BASE_CFLAGS + [f"-D{a['define']}"] + a["cflags"] +
           [f"-I{SHIM_DIR}", "-c", str(SHIM_DIR / "alloc_shim.c"), "-o", str(c_obj)])
    r = run(cmd)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        raise SystemExit(f"failed to build C shim for {alloc_key}")

    cpp_obj = outdir / "alloc_shim_new.o"
    cmd = (["clang++", "-O2", "-w", "-std=c++17", f"-D{a['define']}"] + a["cflags"] +
           [f"-I{SHIM_DIR}", "-c", str(SHIM_DIR / "alloc_shim_new.cpp"),
            "-o", str(cpp_obj)])
    r = run(cmd)
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        raise SystemExit(f"failed to build C++ shim for {alloc_key}")

    return c_obj, cpp_obj


def build_benchmark(test, alloc_key, shim_objs, verbose):
    spec = BENCHMARKS[test]
    a = ALLOCATORS[alloc_key]
    c_obj, cpp_obj = shim_objs
    outdir = BUILD / alloc_key
    binary = outdir / (spec.get("binary") or test)

    sources = [str(SUITE / s) for s in spec["srcs"]]
    defines = [f"-D{d}" for d in spec.get("defines", [])]
    includes = [f"-I{SUITE / i}" for i in spec.get("includes", [])]
    libs = [f"-l{l}" for l in spec.get("libs", [])]

    if spec["lang"] == "c":
        cmd = (["clang"] + BASE_CFLAGS + defines + includes + a["cflags"] +
               [f"-I{SHIM_DIR}", "-include", str(SHIM_DIR / "alloc_shim.h")] +
               sources + [str(c_obj)] + a["ldflags"] + libs +
               ["-lpthread", "-o", str(binary)])
    else:
        cmd = (["clang++", "-O2", "-w", "-std=c++17"] + spec.get("cxxflags", []) +
               defines + includes + a["cflags"] + [f"-I{SHIM_DIR}"] +
               sources + [str(c_obj), str(cpp_obj)] + a["ldflags"] + libs +
               ["-lpthread", "-o", str(binary)])

    r = run(cmd)
    if r.returncode != 0:
        return None, r.stderr
    return binary, None


def score(spec, elapsed, output):
    """Convert one run into a comparable time in seconds (lower is better)."""
    metric = spec["metric"]
    if metric == "wall":
        return elapsed
    m = re.search(spec["regex"], output)
    if not m:
        return None
    value = float(m.group(1))
    if metric == "regex":
        return value
    if value <= 0:
        return None
    return spec["ops_base"] / value


def run_once(binary, spec, procs, timeout):
    args = []
    for arg in spec.get("args", []):
        if arg == "@PROCS":
            args.append(str(procs))
        elif arg == "@PROCS16":
            args.append(str(min(procs, 16)))
        else:
            args.append(arg)

    stdin_file = spec.get("stdin")
    fh = open(stdin_file) if stdin_file else subprocess.DEVNULL
    try:
        start = time.perf_counter()
        try:
            p = subprocess.run([str(binary)] + args, stdin=fh, timeout=timeout,
                               capture_output=True, text=True,
                               cwd=str(binary.parent))
        except subprocess.TimeoutExpired:
            return None, "timeout"
        elapsed = time.perf_counter() - start
    finally:
        if stdin_file:
            fh.close()

    if p.returncode != 0:
        sig = -p.returncode if p.returncode < 0 else p.returncode
        detail = "signal %d" % sig if p.returncode < 0 else "exit %d" % sig
        tail = (p.stderr or p.stdout or "").strip().splitlines()
        if tail:
            detail += ": " + tail[-1][:80]
        return None, detail

    value = score(spec, elapsed, p.stdout + p.stderr)
    if value is None:
        return None, "could not parse output"
    return value, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tests", default="all")
    ap.add_argument("--allocs", default="sys,cm,mi,je,tc")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--procs", type=int, default=os.cpu_count())
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--out", default=str(RESULTS / "results.json"))
    args = ap.parse_args()

    if args.list:
        for name, spec in BENCHMARKS.items():
            kind = "multi-threaded" if spec["threads"] else "single-threaded"
            print(f"{name:16s} {spec['lang']:3s} {kind}")
        return

    if not SUITE.exists():
        raise SystemExit(f"mimalloc-bench sources not found at {SUITE}")

    tests = list(BENCHMARKS) if args.tests == "all" else args.tests.split(",")
    for t in tests:
        if t not in BENCHMARKS:
            raise SystemExit(f"unknown benchmark: {t}")
    allocs = args.allocs.split(",")
    for a in allocs:
        if a not in ALLOCATORS:
            raise SystemExit(f"unknown allocator: {a}")

    BUILD.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)

    print(f"machine: {os.uname().machine}, procs={args.procs}, reps={args.reps}")
    print("building cmalloc dylib...")
    build_cmalloc_dylib(args.verbose)

    shims = {}
    for a in allocs:
        print(f"building shim: {ALLOCATORS[a]['name']}")
        shims[a] = build_shim(a, args.verbose)

    binaries = {}
    for t in tests:
        for a in allocs:
            binary, err = build_benchmark(t, a, shims[a], args.verbose)
            if binary is None:
                print(f"  BUILD FAILED {t} [{a}]")
                if args.verbose:
                    print(err)
            binaries[(t, a)] = binary

    results = {}
    for t in tests:
        spec = BENCHMARKS[t]
        print(f"\n== {t}")
        for a in allocs:
            binary = binaries[(t, a)]
            if binary is None:
                results[(t, a)] = ("build failed", None)
                continue
            values, error = [], None
            for _ in range(args.reps):
                value, err = run_once(binary, spec, args.procs, args.timeout)
                if err:
                    error = err
                    break
                values.append(value)
            if error:
                results[(t, a)] = (error, None)
                print(f"   {ALLOCATORS[a]['name']:9s} FAILED  ({error})")
            else:
                best = min(values)
                med = statistics.median(values)
                results[(t, a)] = (None, best)
                print(f"   {ALLOCATORS[a]['name']:9s} {best:8.3f}s  (median {med:.3f}s)")

    print_table(tests, allocs, results)
    save(args.out, tests, allocs, results, args)


def print_table(tests, allocs, results):
    print("\n\nRelative to cmalloc (1.00 = same speed, >1.00 = slower than cmalloc)")
    print("cmalloc column is absolute seconds.\n")

    header = f"{'benchmark':16s}"
    for a in allocs:
        header += f"{ALLOCATORS[a]['name']:>12s}"
    print(header)
    print("-" * len(header))

    for t in tests:
        row = f"{t:16s}"
        err, cm = results.get((t, "cm"), (None, None))
        for a in allocs:
            aerr, val = results.get((t, a), ("n/a", None))
            if aerr:
                row += f"{'FAIL':>12s}"
            elif a == "cm":
                row += f"{val:>11.3f}s"
            elif cm and val:
                row += f"{val / cm:>11.2f}x"
            else:
                row += f"{val:>11.3f}s"
        print(row)


def save(path, tests, allocs, results, args):
    data = {
        "machine": os.uname().machine,
        "procs": args.procs,
        "reps": args.reps,
        "results": {
            t: {a: {"error": results[(t, a)][0], "seconds": results[(t, a)][1]}
                for a in allocs if (t, a) in results}
            for t in tests
        },
    }
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2))
    print(f"\nwrote {path}")


if __name__ == "__main__":
    main()
