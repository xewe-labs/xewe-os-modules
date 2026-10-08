"""Buttons module tests. Run through a harness: `python -m xewe test --module buttons`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered).
The add/remove test configures one pin as an input (pull-up); choose a free pin with
XEWE_TEST_BUTTONS_PIN (skipped when unset).
"""
import os
import re
from pathlib import Path

import pytest

ID = "buttons"
NAME = "Buttons"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_add_then_remove(serial):
    pin = os.environ.get("XEWE_TEST_BUTTONS_PIN")
    if not pin:
        pytest.skip("needs XEWE_TEST_BUTTONS_PIN (a free GPIO)")
    serial.command(f'$buttons add {pin} "$system status" pullup on_press 50',
                   expect=r"Successfully added button mapping\.", timeout=5)
    # the table lists ID | Pin | ...; the new mapping has the highest id
    serial.send(f"${ID} status")
    ids = [int(m[1]) for line in serial.collect() if (m := re.match(r"^\|\s*(\d+)\s*\|", line.strip()))]
    assert ids, "no button id in status table"
    serial.command(f"$buttons remove {max(ids)}", expect=r"Successfully removed button mapping\.", timeout=5)


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
