"""
On-device Unity test runner for ESP Layout Controller.

Resets the ESP32-S3 via RTS/DTR, captures serial output, and asserts
that all Unity tests pass. Uses pyserial directly for reliable
Windows + ESP32-S3 USB-JTAG serial support.
"""
import re
import serial
import time
import sys


BAUD = 115200
TIMEOUT_S = 30
UNITY_SUMMARY_RE = re.compile(r"(\d+) Tests (\d+) Failures (\d+) Ignored")


def reset_device(ser: serial.Serial) -> None:
    """Hard-reset the ESP32-S3 via RTS/DTR toggling."""
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    ser.dtr = False


def run_tests(port: str) -> bool:
    """Reset device, capture serial output, and check Unity results."""
    ser = serial.Serial(port, BAUD, timeout=1)
    ser.reset_input_buffer()
    reset_device(ser)

    output = ""
    deadline = time.monotonic() + TIMEOUT_S
    match = None

    while time.monotonic() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            output += text
            sys.stdout.write(text)
            sys.stdout.flush()

            match = UNITY_SUMMARY_RE.search(output)
            if match and "OK" in output[output.index(match.group()):]:
                break

    ser.close()

    if not match:
        print("\n\nFAILED: Unity summary line not found within timeout.")
        return False

    total, failures, ignored = int(match.group(1)), int(match.group(2)), int(match.group(3))
    print(f"\n\nResults: {total} Tests, {failures} Failures, {ignored} Ignored")

    if failures > 0:
        print("FAILED: There were test failures.")
        return False

    print("PASSED: All tests passed.")
    return True


def test_throttle_unity_output():
    """Pytest entry point — discovers port from --port arg or defaults to COM4."""
    # Support --port argument for compatibility with existing flash_test target
    port = "COM4"
    for i, arg in enumerate(sys.argv):
        if arg == "--port" and i + 1 < len(sys.argv):
            port = sys.argv[i + 1]

    assert run_tests(port), "Unity tests did not pass"


if __name__ == "__main__":
    port = "COM4"
    for i, arg in enumerate(sys.argv):
        if arg == "--port" and i + 1 < len(sys.argv):
            port = sys.argv[i + 1]
    success = run_tests(port)
    sys.exit(0 if success else 1)
