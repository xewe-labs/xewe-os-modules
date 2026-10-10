"""Pins module tests. Run through a harness: `python -m xewe test --module pins`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered).
adc_read only reads; the pin is XEWE_TEST_PINS_ADC_PIN (default 1, an ADC1 pin on C3/C6/S3).
"""
import os

ID = "pins"
NAME = "Pins"          # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_adc_read(serial):
    pin = os.environ.get("XEWE_TEST_PINS_ADC_PIN", "1")
    serial.command(f"$pins adc_read {pin}", expect=r"^\s*\d+\s*$", timeout=5)


def test_claims_listing(serial):
    # core pin registry listing (owned by the pins module)
    serial.command("$pins claims", expect=r"GPIO \d+: \w+|No GPIO claimed", timeout=5)
