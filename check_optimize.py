#!/usr/bin/env python3
import os
import sys
import subprocess
GREEN = "\033[32m"
RED   = "\033[31m"
RESET = "\033[0m"

PASS_LINE = "[PASS] All checks passed."

def run_check(optc_path, file_path):
    cmd = [
        optc_path,
        "-check",
        "--check=mem2reg,adce",
        file_path
    ]

    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10
        )
    except Exception as e:
        return False, f"Execution failed: {e}"

    output = result.stdout.strip().splitlines()
    if not output:
        return False, "No output"

    if output[-1].strip() == PASS_LINE:
        return True, PASS_LINE
    else:
        return False, output[-1]


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input_folder>")
        sys.exit(1)

    input_folder = sys.argv[1]
    optc_path = "./optc"

    if not os.path.isdir(input_folder):
        print(f"Error: {input_folder} is not a directory")
        sys.exit(1)

    if not os.path.isfile(optc_path):
        print("Error: ./optc not found")
        sys.exit(1)

    files = sorted(
        os.path.join(input_folder, f)
        for f in os.listdir(input_folder)
        if os.path.isfile(os.path.join(input_folder, f))
    )

    if not files:
        print("No files found in directory.")
        sys.exit(0)

    passed = 0
    failed = 0

    print(f"Checking {len(files)} files...\n")

    for file_path in files:
        ok, msg = run_check(optc_path, file_path)
        # name = os.path.basename(file_path)

        if ok:
            print(f"{GREEN}[PASS]{RESET} {file_path}")
            passed += 1
        else:
            print(f"{RED}[FAIL]{RESET} {file_path} -> {msg}")
            failed += 1

    print("\n========== Summary ==========")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Total : {passed + failed}")


if __name__ == "__main__":
    main()
