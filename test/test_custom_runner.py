"""Custom PlatformIO test runner for OpenScope's native DSP smoke test.

`platformio.ini` sets `test_framework = custom` for the native env because the
test program (test/test_speed_from_fft/test_main.cpp) has its own `main()` and a
tiny home-grown CHECK macro instead of Unity. The custom framework requires a
`CustomTestRunner` class — without this file `pio test -e native` aborts with
"Could not find custom test runner".

The program prints one line per assertion:
    "ok:   <description>"   on success
    "FAIL: <description>"   on failure
and exits non-zero if any assertion failed. This runner turns each of those
lines into a PlatformIO TestCase so the run reports proper pass/fail counts
(a suite with no registered cases is reported as SKIPPED, even on a clean run).
A non-zero program exit is still surfaced as a failure by the native reader.
"""

from platformio.test.result import TestCase, TestStatus
from platformio.test.runners.base import TestRunnerBase


class CustomTestRunner(TestRunnerBase):
    # Building and running the native program are handled by the base class;
    # we only need to translate the program's output into test cases.
    def on_testing_line_output(self, line):
        super().on_testing_line_output(line)  # keep the program's own output

        stripped = line.strip()
        if stripped.startswith("ok:"):
            self.test_suite.add_case(
                TestCase(name=stripped[len("ok:"):].strip(), status=TestStatus.PASSED)
            )
        elif stripped.startswith("FAIL:"):
            self.test_suite.add_case(
                TestCase(
                    name=stripped[len("FAIL:"):].strip(),
                    status=TestStatus.FAILED,
                    message="assertion failed",
                )
            )
