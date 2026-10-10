"""Scheduler module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module scheduler --unit-only`.
"""

import pytest

from xewe.testing import module_dir

ID = "schedule"
NAME = "Scheduler"          # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp


@pytest.mark.unit
def test_add_sample_commands_start_with_dollar():
    cpp = (MODULE_DIR / "src" / "Scheduler" / "Scheduler.cpp").read_text()
    sample = cpp.split('"$schedule add ', 1)[1].split("\n", 1)[0]
    cmds = sample.split('\\"')[1].split("|")
    assert cmds and all(c.startswith("$") for c in cmds)
