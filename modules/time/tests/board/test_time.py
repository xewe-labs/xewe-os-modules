"""Time module tests. Run through a harness: `python -m xewe test --module time`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi connected with internet access (NTP).
"""

ID = "time"
NAME = "Time"          # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_fetch_syncs_time(serial):
    # "Syncing time from server..." has no line end until the sync finishes (~20 s), so wait for
    # the outcome line only.
    serial.command("$time fetch",
                   expect=r"Current time: \d{4}-\d\d-\d\d \d\d:\d\d:\d\d|unable to reach the time server",
                   timeout=30)


def test_settings_table(serial):
    # settings table: tz_gmt_str row
    serial.command("$time get tz_gmt_str", expect=r"tz_gmt_str=GMT[+-]\d\d:\d\d", timeout=5)
    serial.command("$time schema", expect=r'\{"end":"time","count":1\}', timeout=5)
