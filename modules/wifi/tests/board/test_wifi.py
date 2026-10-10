"""Wifi module tests. Run through a harness: `python -m xewe test --module wifi`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi credentials stored and connected.
"""

ID = "wifi"
NAME = "Wifi"          # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_scan_lists_networks(serial):
    serial.command("$wifi scan", expect=r"Scanning WiFi networks", timeout=5)
    serial.expect(r"^\s*0\. \S", timeout=20)   # numbered, de-duplicated SSIDs


def test_settings_table_masks_password(serial):
    # settings table: ssid + psw (SECRET: never printed)
    serial.command("$wifi get psw", expect=r"psw=\*{8}", timeout=5)
    serial.command("$wifi schema", expect=r'\{"end":"wifi","count":2\}', timeout=5)
