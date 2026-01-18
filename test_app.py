import re

def test_throttle_unity_output(dut):
    dut.expect(re.compile(r"\d+ Tests 0 Failures 0 Ignored"), timeout=60)
    dut.expect_exact("OK", timeout=10)
