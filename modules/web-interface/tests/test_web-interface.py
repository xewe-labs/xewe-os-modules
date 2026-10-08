"""Web Interface module tests. Run through a harness: `python -m xewe test --module web-interface`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi connected (Wifi is a requirement). No HTTP requests are made from tests in phase 1.
"""
from pathlib import Path

import pytest

ID = "web_interface"
NAME = "Web Interface"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_status_reports_server(serial):
    # the module has no commands of its own; its status prints the web server block
    serial.command(f"${ID} status", expect=r"--- Web Server Status ---", timeout=5)
    serial.expect(r"- Uptime:\s+\d+d \d\d:\d\d:\d\d", timeout=5)
    serial.expect(r"- Memory Usage:\s+[\d.]+% \(\d+ / \d+ bytes\)", timeout=5)


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
