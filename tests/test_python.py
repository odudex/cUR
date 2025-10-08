#!/usr/bin/env python3

import sys
import os
import glob
import argparse

# Add the Python implementation to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'foundation-ur-py', 'src'))

from ur.ur_decoder import URDecoder

TEST_CASES_DIR = "test_cases"


def read_fragments_from_file(filepath):
    """Read UR fragments from a file."""
    fragments = []
    try:
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                # Find UR string (case-insensitive)
                line_lower = line.lower()
                ur_index = line_lower.find('ur:')
                if ur_index == -1:
                    continue
                
                # Extract the UR string
                start = ur_index
                end = start
                while end < len(line) and line[end] not in ['"', ',', '\r', '\n']:
                    end += 1
                
                fragment = line[start:end]
                if fragment:
                    fragments.append(fragment)
    except Exception as e:
        print(f"Error reading file {filepath}: {e}")
        return []
    
    return fragments


def test_file(filepath, generate_expected=False):
    """Test a single file and optionally generate expected output."""
    print(f"\n=== Testing file: {filepath} ===")

    fragments = read_fragments_from_file(filepath)

    if not fragments:
        print("No fragments found in file")
        return False

    decoder = URDecoder()
    parts_used = 0
    success = False
    result_data = None

    for fragment in fragments:
        try:
            decoder.receive_part(fragment)
            parts_used += 1

            if decoder.is_complete():
                print("Data reconstruction is complete: YES")

                if decoder.is_success():
                    print("Data reconstruction is success (valid): YES")
                    success = True
                    result_data = decoder.result_message()
                else:
                    print("Data reconstruction is success (valid): NO")

                print(f"Parts used/total available parts: {parts_used}/{len(fragments)}")
                break

        except Exception as e:
            # Silently skip failed parts
            continue

    if not decoder.is_complete():
        print("Data reconstruction is complete: NO")
        print("Data reconstruction is success (valid): NO")
        print(f"Parts used/total available parts: {parts_used}/{len(fragments)}")

    # Generate expected output file if requested
    if generate_expected and success and result_data:
        # Generate output filename: data_X.UR_fragments.txt -> data_X.UR_object.txt
        base_name = os.path.basename(filepath)
        if base_name.endswith('.UR_fragments.txt'):
            expected_file = filepath.replace('.UR_fragments.txt', '.UR_object.txt')
        else:
            expected_file = filepath.replace('.txt', '.expected')

        # Write hex-encoded result (result_data is a UR object with cbor property)
        if hasattr(result_data, 'cbor'):
            data_bytes = result_data.cbor
        elif hasattr(result_data, 'data'):
            data_bytes = result_data.data
        else:
            data_bytes = bytes(result_data)

        hex_result = data_bytes.hex()
        with open(expected_file, 'w') as f:
            f.write(f"hex:{hex_result}\n")
        print(f"Generated expected output: {expected_file}")
        print(f"Expected data (hex): {hex_result}")

    return success

def main():
    parser = argparse.ArgumentParser(description='Test UR decoder implementation')
    parser.add_argument('--generate-expected', action='store_true',
                        help='Generate expected output files for test cases')
    args = parser.parse_args()

    print("=== Python UR Implementation Test ===")

    if args.generate_expected:
        print("Mode: Generating expected output files")

    # Get all fragment files in test_cases directory
    test_files = sorted(glob.glob(os.path.join(TEST_CASES_DIR, "*UR_fragments.txt")))

    # Fallback to old naming if no new naming found
    if not test_files:
        test_files = sorted(glob.glob(os.path.join(TEST_CASES_DIR, "fragments_*.txt")))

    if not test_files:
        print(f"No test files found in {TEST_CASES_DIR}")
        return 1

    total_tests = 0
    passed_tests = 0

    for filepath in test_files:
        total_tests += 1
        if test_file(filepath, generate_expected=args.generate_expected):
            passed_tests += 1

    print("\n=== Summary ===")
    print(f"Tests passed: {passed_tests}/{total_tests}")

    return 0 if passed_tests == total_tests else 1


if __name__ == "__main__":
    sys.exit(main())