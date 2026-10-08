"""Wifi module tests. Run through a harness: `python -m xewe test --module wifi`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi credentials stored and connected.
"""
from pathlib import Path

import pytest

ID = "wifi"
NAME = "Wifi"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_scan_lists_networks(serial):
    serial.command("$wifi scan", expect=r"Scanning WiFi networks", timeout=5)
    serial.expect(r"^\s*0\. \S", timeout=20)   # numbered, de-duplicated SSIDs


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
