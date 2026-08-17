#!/usr/bin/env python3
"""
Unified Automated Test Runner (`scripts/run_all_tests.py`)

Runs the entire test suite for the Remote 3-Axis Magnetometer project,
including binary serialization, parser logic, mathematical scaling invariance,
and Central Server API/database integration tests.

Usage:
    python3 scripts/run_all_tests.py
    python3 scripts/run_all_tests.py --verbose
"""

import os
import sys
import time
import subprocess
import argparse

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

TEST_SUITES = [
    {
        "name": "Batch Struct Binary Serialization",
        "cmd": [sys.executable, "test/test_batch_serialization.py"],
        "cwd": REPO_ROOT
    },
    {
        "name": "Receiver Stream & CSV Parsing",
        "cmd": [sys.executable, "test/test_receiver_parser.py"],
        "cwd": REPO_ROOT
    },
    {
        "name": "RM3100 Dynamic Scaling Math Invariance",
        "cmd": [sys.executable, "test/test_scaling_math.py"],
        "cwd": REPO_ROOT
    },
    {
        "name": "Central Server API, DB Migrations & API Key Auth",
        "cmd": [sys.executable, "central_service/test_server.py"],
        "cwd": REPO_ROOT
    }
]

def run_tests(verbose: bool = False) -> bool:
    print("\n========================================================")
    print("      REMOTE 3-AXIS MAGNETOMETER AUTOMATED TEST SUITE   ")
    print("========================================================\n")
    
    total_start = time.time()
    results = []
    all_passed = True

    for suite in TEST_SUITES:
        name = suite["name"]
        cmd = suite["cmd"]
        cwd = suite.get("cwd", REPO_ROOT)
        
        print(f"[*] Running: {name}...", end="", flush=True)
        t0 = time.time()
        
        res = subprocess.run(
            cmd,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )
        duration = time.time() - t0
        passed = (res.returncode == 0)

        if passed:
            print(f" \033[92m[PASS]\033[0m ({duration:.2f}s)")
            results.append((name, True, duration, ""))
        else:
            print(f" \033[91m[FAIL]\033[0m ({duration:.2f}s)")
            results.append((name, False, duration, res.stdout))
            all_passed = False

        if verbose or not passed:
            print("-" * 56)
            print(res.stdout.strip())
            print("-" * 56)

    total_duration = time.time() - total_start
    print("\n========================================================")
    print("                    TEST SUMMARY                        ")
    print("========================================================")
    for name, passed, duration, _ in results:
        status_str = "\033[92mPASS\033[0m" if passed else "\033[91mFAIL\033[0m"
        print(f" - {name:<50} [{status_str}] ({duration:.2f}s)")
    print("--------------------------------------------------------")
    
    if all_passed:
        print(f"\033[92mSUCCESS:\033[0m All {len(TEST_SUITES)} test suites passed in {total_duration:.2f}s!\n")
        return True
    else:
        print(f"\033[91mFAILURE:\033[0m One or more test suites failed.\n")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run all automated unit and integration tests.")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print verbose test outputs")
    args = parser.parse_args()

    success = run_tests(verbose=args.verbose)
    sys.exit(0 if success else 1)
