#!/usr/bin/env python
import argparse
import glob
import os
import subprocess
import sys

import xlsxwriter as xl  # pip install xlsxwriter
import xlsxwriter.utility as xlutil

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
CHAL_DIR = os.path.join(os.path.dirname(TOOLS_DIR), 'processed-challenges')
TEST_DIR = os.path.join(TOOLS_DIR, 'cb-testing')


def debug(s):
    sys.stdout.write(str(s))
    sys.stdout.flush()


class Score:
    """Contains the results of a test"""

    def __init__(self):
        self.passed = 0
        self.total = 0

    @property
    def failed(self):
        """Number of failed tests"""
        return self.passed - self.total


class Tester:
    """Tests and keeps track of the results of a single challenge binary"""

    # These determine which types of tests will be run
    # Both are enabled by default
    povs_enabled = True
    polls_enabled = True

    def __init__(self, chal_name):
        self.name = chal_name

        # Directories used in testing
        self.chal_dir = os.path.join(CHAL_DIR, self.name)
        self.bin_dir = os.path.join(self.chal_dir, 'bin')
        self.pov_dir = os.path.join(self.chal_dir, 'pov')
        self.poll_dir = os.path.join(self.chal_dir, 'poller')

        # Keep track of success
        self.povs = Score()
        self.polls = Score()

    @property
    def passed(self):
        """Number of passed tests"""
        return self.povs.passed + self.polls.passed

    @property
    def total(self):
        """Total number of tests run"""
        return self.povs.total + self.polls.total

    @property
    def failed(self):
        """Number of failed tests"""
        return self.total - self.passed

    @staticmethod
    def parse_results(output):
        """ Parses out the number of passed and failed tests from cb-test output

        Args:
            output (str): Raw output from running cb-test
        Returns:
            (int, int): # of tests passed, # of tests failed
        """
        # If the test failed to run, consider it failed
        if 'polls passed' not in output:
            return 0, 1

        if 'timed out' in output:
            debug('WARNING: test timed out')

        # Parse out results
        passed = int(output.split('polls passed: ')[1].split('\n')[0])
        failed = int(output.split('polls failed: ')[1].split('\n')[0])
        return passed, failed

    def run_test(self, bin_names, xml_dir, score):
        """ Runs a test using cb-test and saves the result

        Args:
            bin_names (list of str): Name of the binary being tested
            xml_dir (str): Directory containing all xml tests
            score (Score): Object to store the results in
        """
        cb_cmd = ['./cb-test', '--directory', self.bin_dir, '--xml_dir', xml_dir,
                  '--concurrent', '8', '--timeout', '5', '--cb'] + bin_names
        p = subprocess.Popen(cb_cmd, cwd=TEST_DIR, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()

        passed, failed = self.parse_results(out)
        score.passed += passed
        score.total += passed + failed

    def run_against_dir(self, xml_dir, score):
        """ Runs all tests in a given directory
        against the patched and unpatched versions of a binary

        Args:
            xml_dir (str): Directory containing all xml tests
            score (Score): Object to store the results in
        """
        # Check if there are any tests available in this directory
        tests = glob.glob(os.path.join(xml_dir, '*.xml'))
        if len(tests) == 0:
            debug('None found\n')
            return

        # *2 because each test is run against the patched and unpatched binary
        debug('Running {} test(s)'.format(len(tests) * 2))

        # Collect the names of binaries to be tested
        cb_dirs = glob.glob(os.path.join(self.chal_dir, 'cb_*'))
        if len(cb_dirs) > 0:
            # There are multiple binaries in this challenge
            bin_names = ['{}_{}'.format(self.name, i + 1) for i in range(len(cb_dirs))]
        else:
            bin_names = [self.name]

        # Keep track of old pass/totals
        p, t = score.passed, score.total

        # Run the tests
        self.run_test(bin_names, xml_dir, score)
        self.run_test(['{}_patched'.format(b) for b in bin_names], xml_dir, score)

        # Display resulting totals
        debug(' => Passed {}/{}\n'.format(score.passed - p, score.total - t))

    def run(self):
        """Runs all tests for this challenge binary"""
        debug('\nTesting {}...\n'.format(self.name))

        # Test POVs
        if Tester.povs_enabled:
            debug('POV:\n\t')
            self.run_against_dir(self.pov_dir, self.povs)

        # Test POLLs
        if Tester.polls_enabled:
            debug('POLL:\n')
            for subdir in listdir(self.poll_dir):
                debug('\t{}:\t'.format(subdir))
                self.run_against_dir(os.path.join(self.poll_dir, subdir), self.polls)
        debug('Done testing {} => Passed {}/{} tests\n'.format(self.name, self.passed, self.total))


def test_challenges(chal_names):
    # type: (list) -> list
    # Filter out any challenges that don't exist
    chals = []
    for c in chal_names:
        cdir = os.path.join(CHAL_DIR, c)
        if not os.path.isdir(cdir):
            debug('ERR: Challenge "{}" does not exist, skipping\n'.format(c))
            continue

        # Skip duplicates
        if c in chals:
            debug('Ignoring duplicate "{}"\n'.format(c))
            continue

        chals.append(c)

    # Create and run all testers
    testers = map(Tester, chals)
    for test in testers:
        test.run()

    return testers


def generate_xlsx(path, tests):
    """ Generates an excel spreadsheet containing the results of all tests

    Args:
        path (str): Path to save the spreadsheet
        tests (list of Tester): All completed tests
    """
    debug('Generating excel spreadsheet...')
    # Fix filename
    if not path.endswith('.xlsx'):
        path += '.xlsx'

    wb = xl.Workbook(path)
    ws = wb.add_worksheet()

    # Some cell formats used in the sheet
    fmt_name = wb.add_format({'font_color': 'green', 'bg_color': 'black'})
    fmt_perfect = wb.add_format({'bg_color': '#b6d7a8', 'border': 1, 'border_color': '#cccccc'})
    fmt_bad = wb.add_format({'bg_color': '#ea9999', 'border': 1, 'border_color': '#cccccc'})
    fmt_none = wb.add_format({'bg_color': '#ffe599', 'border': 1, 'border_color': '#cccccc'})
    fmt_default = wb.add_format({'bg_color': 'white', 'border': 1, 'border_color': '#cccccc'})

    # Some common format strings
    subtract = '={}-{}'
    add = '={}+{}'
    percent = '=100*{}/MAX(1, {})'

    # Write headers
    cols = ['CB_NAME',
            'POVs Total', 'POVs Passed', 'POVs Failed', '% POVs Passed', '',
            'POLLs Total', 'POLLs Passed', 'POLLs Failed', '% POLLs Passed', '',
            'Total Tests', 'Total Passed', 'Total Failed', 'Total % Passed',
            'Notes']
    row = 0
    ws.write_row(row, 0, cols)

    # Helper map for getting column indices
    col_to_idx = {val: i for i, val in enumerate(cols)}

    # Helper for writing formulas that use two cells
    def write_formula(row, col_name, formula, formula_col1, formula_col2):
        ws.write_formula(row, col_to_idx[col_name],
                         formula.format(xlutil.xl_rowcol_to_cell(row, col_to_idx[formula_col1]),
                                        xlutil.xl_rowcol_to_cell(row, col_to_idx[formula_col2])))

    # Add all test data
    for test in tests:
        row += 1

        # Pick the format for this row
        if test.total == 0:
            fmt = fmt_none
        elif test.total == test.passed:
            fmt = fmt_perfect
        elif test.passed == 0:
            fmt = fmt_bad
        else:
            fmt = fmt_default

        # Apply this format to the whole row
        ws.conditional_format(row, col_to_idx['POVs Total'],
                              row, col_to_idx['Total % Passed'], {
            'type': 'formula',
            'criteria': 'TRUE',  # "conditional"
            'format': fmt
        })

        # Write some fields we already know
        ws.write(row, 0, test.name, fmt_name)
        ws.write_row(row, col_to_idx['POVs Total'], [test.povs.total, test.povs.passed])
        ws.write_row(row, col_to_idx['POLLs Total'], [test.polls.total, test.polls.passed])

        # NOTE: Leaving all of these to be calculated in excel in case you want to manually edit it later

        # POVs
        write_formula(row, 'POVs Failed', subtract, 'POVs Total', 'POVs Passed')
        write_formula(row, '% POVs Passed', percent, 'POVs Passed', 'POVs Total')

        # POLLs
        write_formula(row, 'POLLs Failed', subtract, 'POLLs Total', 'POLLs Passed')
        write_formula(row, '% POLLs Passed', percent, 'POLLs Passed', 'POLLs Total')

        # Totals
        write_formula(row, 'Total Tests', add, 'POVs Total', 'POLLs Total')
        write_formula(row, 'Total Passed', add, 'POVs Passed', 'POLLs Passed')
        write_formula(row, 'Total Failed', subtract, 'Total Tests', 'Total Passed')
        write_formula(row, 'Total % Passed', percent, 'Total Passed', 'Total Tests')

    # These columns are ignored in totals
    skip_cols = ['', 'CB_NAME', '% POVs Passed', '% POLLs Passed', 'Total % Passed', 'Notes']

    # Totals at bottom
    row += 1
    ws.write(row, 0, 'TOTAL')
    for col_name in cols:
        if col_name not in skip_cols:
            col = col_to_idx[col_name]
            ws.write_formula(row, col, '=SUM({})'.format(xlutil.xl_range(1, col, len(tests), col)))

    # Calculate total %'s
    write_formula(row, '% POVs Passed', percent, 'POVs Passed', 'POVs Total')
    write_formula(row, '% POLLs Passed', percent, 'POLLs Passed', 'POLLs Total')
    write_formula(row, 'Total % Passed', percent, 'Total Passed', 'Total Tests')

    # These columns are ignored in averages
    skip_cols = ['', 'CB_NAME', 'Notes']

    # Averages at bottom
    row += 1
    ws.write(row, 0, 'AVERAGE')
    for col_name in cols:
        if col_name not in skip_cols:
            col = col_to_idx[col_name]
            ws.write_formula(row, col, '=AVERAGE({})'.format(xlutil.xl_range(1, col, len(tests), col)))

    # Done, save the spreadsheet
    wb.close()
    debug('Done, saved to {}\n'.format(path))


def listdir(path):
    # type: (str) -> list
    if not os.path.isdir(path):
        return []
    return sorted(os.listdir(path), key=lambda s: s.lower())


def main():
    parser = argparse.ArgumentParser()

    g = parser.add_mutually_exclusive_group(required=True)
    g.add_argument('-a', '--all', action='store_true',
                   help='Run tests against all challenge binaries')

    g.add_argument('-c', '--chals', nargs='+', type=str,
                   help='List of challenge names to test')

    g = parser.add_mutually_exclusive_group()
    g.add_argument('--povs', action='store_true',
                   help='Only run tests against POVs')

    g.add_argument('--polls', action='store_true',
                   help='Only run tests against POLLS')

    parser.add_argument('-o', '--output',
                        default=None, type=str,
                        help='If provided, an excel spreadsheet will be generated and saved here')

    args = parser.parse_args(sys.argv[1:])

    # Disable other tests depending on args
    if args.povs:
        Tester.polls_enabled = False
    if args.polls:
        Tester.povs_enabled = False

    if args.all:
        debug('Running tests against all challenges\n')
        tests = test_challenges(listdir(CHAL_DIR))
    else:
        debug('Running tests against {} challenge(s)\n'.format(len(args.chals)))
        tests = test_challenges(args.chals)

    if args.output:
        generate_xlsx(os.path.abspath(args.output), tests)


if __name__ == '__main__':
    main()
