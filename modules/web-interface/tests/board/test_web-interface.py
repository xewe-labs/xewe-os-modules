"""Web Interface module tests. Run through a harness: `python -m xewe test --module web-interface`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi connected (Wifi is a requirement). The tests make no HTTP requests.
"""

ID = "web_interface"
NAME = "Web Interface"          # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_status_reports_server(serial):
    # the module has no commands of its own; its status prints the web server block
    serial.command(f"${ID} status", expect=r"Uptime: \d+d \d\d:\d\d:\d\d", timeout=5)
    serial.expect(r"- Uptime:\s+\d+d \d\d:\d\d:\d\d", timeout=5)
    serial.expect(r"- Memory Usage:\s+[\d.]+% \(\d+ / \d+ bytes\)", timeout=5)


def test_settings_table(serial):
    # settings table: port (restart) + root rows
    serial.command("$web_interface get port", expect=r"port=\d+", timeout=5)
    serial.command("$web_interface schema", expect=r'\{"end":"web_interface","count":2\}', timeout=5)
