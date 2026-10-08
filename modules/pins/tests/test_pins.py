"""Pins module tests. Run through a harness: `python -m xewe test --module pins`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered).
adc_read only reads; the pin is XEWE_TEST_PINS_ADC_PIN (default 1, an ADC1 pin on C3/C6/S3).
"""
import os
from pathlib import Path

import pytest

ID = "pins"
NAME = "Pins"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_adc_read(serial):
    pin = os.environ.get("XEWE_TEST_PINS_ADC_PIN", "1")
    serial.command(f"$pins adc_read {pin}", expect=r"^\s*\d+\s*$", timeout=5)


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
