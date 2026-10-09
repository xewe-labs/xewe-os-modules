"""Scheduler module tests. Run through a harness: `python -m xewe test --module scheduler`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
Time (and Wifi) enabled; the boot after first setup done (the schedule is active after it).
"""
import re

ID = "schedule"
NAME = "Scheduler"          # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_add_then_remove(serial):
    # add a block (Sunday 23:59, harmless command), find its id in the status JSON, remove it again
    serial.command('$schedule add 1439 1439 6 00FF00 "$system status"',
                   expect=r"Scheduler: schedule saved", timeout=5)
    serial.send(f"${ID} status")
    ids = [int(m) for line in serial.collect() for m in re.findall(r'"id"\s*:\s*(\d+)', line)]
    assert ids, "no schedule id in status output"
    serial.command(f"$schedule remove {max(ids)}", expect=r"Scheduler: schedule removed", timeout=5)
