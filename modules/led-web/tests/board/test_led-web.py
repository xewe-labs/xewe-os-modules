"""Led Web module tests. Run through a harness: `python -m xewe test --module led-web`.

Hardware preconditions: provisioned board (first boot done), WiFi connected (web-interface needs it).
led-strip and led-modes are compiled in as dependencies (the harness lock lists FastLED).

What these tests cannot cover: the tools have no HTTP client and the CLI cannot issue a GET, so the
routes are checked by the unit tests (tests/unit/test_led-web.py), which parse the route table from
LedWeb.cpp and the URLs the page's JavaScript requests. The page itself (sliders, polling, mode switching) needs a browser on the same
network and is not tested here.
"""

ID = "led_web"
NAME = "Led Web"      # module.properties name == C++ name argument


def test_compiles(compiled):
    # build of the harness firmware (web-interface, led-strip, led-modes, led-web) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Page:\s+http://[\d.]+/led", timeout=5)


def test_url_command(serial):
    # unverified: written without a board (2026-10-09 night run); regex copied from the `url` handler
    serial.command(f"${ID} url", expect=r"Led Web: http://\d+\.\d+\.\d+\.\d+/led", timeout=5)
