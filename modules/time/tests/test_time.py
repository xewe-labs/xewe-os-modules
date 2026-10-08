"""Time module tests. Run through a harness: `python -m xewe test --module time`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi connected with internet access (NTP).
"""
from pathlib import Path

import pytest

ID = "time"
NAME = "Time"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_fetch_syncs_time(serial):
    # "Syncing time from server..." has no line end until the sync finishes (~20 s), so wait for
    # the outcome line only.
    serial.command("$time fetch",
                   expect=r"Current time: \d{4}-\d\d-\d\d \d\d:\d\d:\d\d|Unable to reach time server",
                   timeout=30)


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
