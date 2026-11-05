"""On-board smoke test. Requires pytest-embedded-idf and an ESP32 serial port."""
import pytest


@pytest.mark.esp32
def test_serial_shell(dut):
    dut.expect_exact("|_____||____/ |_|")
    dut.expect_exact("Version: V0.0.1")
    dut.expect_exact("ctshell 1.1.0 (2f48ab4)")
    dut.expect_exact("al1s> ")
    dut.write("ping")
    dut.expect_exact("PONG!")
    dut.expect_exact("al1s> ")
    dut.write("version")
    dut.expect_exact("ctshell 1.1.0 (2f48ab4)")
    dut.expect_exact("al1s> ")
    dut.write("sys info")
    dut.expect_exact("shell core: 0")
    dut.expect_exact("al1s> ")
