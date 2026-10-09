"""Buttons module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module buttons --unit-only`.
"""

import pytest

from xewe.testing import module_dir

ID = "buttons"
NAME = "Buttons"          # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
