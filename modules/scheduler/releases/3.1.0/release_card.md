# scheduler 3.1.0 release card

| Field | Value |
|---|---|
| Repository | xewe-labs/xewe-os-modules |
| Release | 3.1.0 (tag `3.1.0`) |
| Card generated | 2026-10-20T17:24:40Z, run `20261020T165327Z-b509`, xewe-os-tools 3.1.2 (`b7f039f6a8d291feeb0b4f1e7c39f709d7dc0c97`) |
| Tested commit | `4ebe734458e7927d80cb94cafeeae76a6a3451e3` |
| Tested tree | `sha256:30e2897ee6bfdfe92f4135645a9b20e604beb16b37b5526434dc9be746476128` |
| Result | 2 unit passed; c3: 2 passed on hardware (board sn=AC:27:6E:73:C0:B4), 1 compile only; c6: 1 compile only, 2 compiled, not run; s3: 1 compile only, 2 compiled, not run; 0 skipped; 0 failed |

## 1. Release notes

```text
scheduler 3.1.0

No code change since scheduler 3.0.0. This release raises requires_core to >=3.1.0,<4.0.0, so the whole
module set builds against XeWeCore 3.1.0, and its release card is generated again from the 3.1.0
test run (unit tests, module-alone and all-modules builds on c3, c6 and s3, and the board session).

Requirements

- XeWeCore >=3.1.0,<4.0.0 (requires_core). Everything else as in scheduler 3.0.0.
```

## 2. Metadata

### Pins

| Component | Ref | Commit | Source |
|---|---|---|---|
| xewe-os-core | 3.1.0 | e2c18cbd73cd5b5adc85f95ecc0ea5d2db336672 | https://github.com/xewe-labs/xewe-os-core |
| xewe-os-modules | 3.0.0 | - | local:~/todo/xewe-labs/xewe-os-modules/build/release-venv/xewe-os-modules |
| xewe-os-tools | 3.1.2 | e17db0a9933f3107a2b5f175e781a6d6a122e0d3 | https://github.com/xewe-labs/xewe-os-tools |
| ArduinoJson | v7.4.3 | 77771d3c07668e01d8f52acb03910c1110bb373f | https://github.com/bblanchon/ArduinoJson |
| FastLED | 3.10.3 | 20667c3a6413ed46a828f78ec95fb57e58d753f8 | https://github.com/FastLED/FastLED |
| WebSockets | 77024d2723788044ac0715bccd2f42e84bdb2817 | 77024d2723788044ac0715bccd2f42e84bdb2817 | https://github.com/xewe-labs/xewe-led-library-websockets |

### Toolchain and host

| xewe-os-tools | arduino-cli | esp32 core | Python | Host |
|---|---|---|---|---|
| 3.1.2 | 1.5.1 | 3.3.12 | 3.13.5 | Linux x86_64 |

### Images

| Chip | FQBN | Partition scheme | App partition (B) | Flash used (B) | % | Static RAM (B) | RAM max (B) | Warnings | Log |
|---|---|---|---|---|---|---|---|---|---|
| c3 | `esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,DebugLevel=none,EraseFlash=all,FlashMode=qio,FlashSize=4M,PartitionScheme=no_ota,UploadSpeed=921600` | no_ota | 2,097,152 | 1,508,041 | 71.9 | 50,168 | 327,680 | 0 | 33-build-c3-all-modules.log |
| c6 | `esp32:esp32:esp32c6:CDCOnBoot=cdc,CPUFreq=160,DebugLevel=none,EraseFlash=all,FlashMode=qio,FlashSize=4M,PartitionScheme=no_ota,UploadSpeed=921600` | no_ota | 2,097,152 | 1,550,326 | 73.9 | 57,100 | 327,680 | 0 | 36-build-c6-all-modules.log |
| s3 | `esp32:esp32:esp32s3:CDCOnBoot=cdc,CPUFreq=160,DebugLevel=none,EraseFlash=all,FlashMode=qio,FlashSize=4M,PartitionScheme=no_ota,UploadSpeed=921600` | no_ota | 2,097,152 | 1,362,864 | 65.0 | 59,784 | 327,680 | 6 | 38-build-s3-all-modules.log |

## 3. Boards

| Chip | Board id | Port | Defines | Firmware sha256 | Meta timestamp | Banner match | Flash step | Flash time |
|---|---|---|---|---|---|---|---|---|
| c3 | AC:27:6E:73:C0:B4 | /dev/xewe-usb/AC-27-6E-73-C0-B4 | - | `ecfc3874f13e230962d5c0a7ec64b12a4f13677039a5e70ef9d7108cbcd810ec` | 2026-10-20T17:11:26Z | yes | 34-flash-c3 | 9.6 s |

## 4. Validator and checks

| Step | Command | Exit | Time | Summary line |
|---|---|---|---|---|
| 1-setup-venv | `/usr/bin/python3 -m venv --clear build/release-venv/py` | 0 | 2.5 s | - |
| 2-setup-install | `build/release-venv/py/bin/python -m pip install --disable-pip-version-check --progress-bar off git+https://github.com/xewe-labs/xewe-os-tools@3.1.2 ruff==0.17.0 clang-format==23.1.3` | 0 | 6.5 s | Successfully installed clang-format-23.1.3 iniconfig-2.3.1 packaging-26.3 pluggy-1.7.0 pygments-2.21.0 pyserial-3.5 pyt… |
| 3-setup-harness-project | `env -u XEWE_PROJECT 'XEWE_MODULES_SOURCE=~/todo/xewe-labs/xewe-os-modules/build/release-venv/xewe-os-modules' XEWE_NO_BOARD=1 bash build/release-harness/project/setup.sh --modules all` | 0 | 14.0 s | setup complete: arduino-cli 1.5.1, esp32 3.3.12, core 3.1.0, modules 3.0.0 (buttons, led, wifi, web-interface, led-web,… |
| 4-setup-harness-alone | `env -u XEWE_PROJECT 'XEWE_MODULES_SOURCE=~/todo/xewe-labs/xewe-os-modules/build/release-venv/xewe-os-modules' XEWE_NO_BOARD=1 bash build/release-harness/alone/setup.sh --modules all` | 0 | 14.0 s | setup complete: arduino-cli 1.5.1, esp32 3.3.12, core 3.1.0, modules 3.0.0 (buttons, led, wifi, web-interface, led-web,… |
| 5-validate-validate | `build/release-harness/project/build/tools/.venv/bin/python build/release-venv/xewe-os-modules/tools/validate.py --harness build/release-harness/project` | 0 | 0.2 s | 8 modules, 0 errors, 0 warnings |
| 7-check-format | `env XEWE_CLANG_FORMAT=build/release-venv/py/bin/clang-format build/release-venv/py/bin/python -m xewe format --check modules tools` | 0 | 3.0 s | format: 62 C++ and 18 Python files, 0 would reformat |
| 8-check-brand-lint | `build/release-venv/py/bin/python -m xewe brand-lint .` | 0 | 0.3 s | brand-lint: clean |

#### 1-setup-venv (last 20 lines of 1-setup-venv.log)

```text
```

#### 2-setup-install (last 20 lines of 2-setup-install.log)

```text
Collecting pluggy<2,>=1.5 (from pytest>=8.0->xewe-os-tools==3.1.2)
  Using cached pluggy-1.7.0-py3-none-any.whl.metadata (5.0 kB)
Collecting pygments>=2.7.2 (from pytest>=8.0->xewe-os-tools==3.1.2)
  Using cached pygments-2.21.0-py3-none-any.whl.metadata (2.5 kB)
Using cached ruff-0.17.0-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl (10.4 MB)
Using cached clang_format-23.1.3-py2.py3-none-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl (1.9 MB)
Using cached pyserial-3.5-py2.py3-none-any.whl (90 kB)
Using cached pytest-9.1.1-py3-none-any.whl (386 kB)
Using cached pluggy-1.7.0-py3-none-any.whl (27 kB)
Using cached iniconfig-2.3.1-py3-none-any.whl (7.6 kB)
Using cached packaging-26.3-py3-none-any.whl (129 kB)
Using cached pygments-2.21.0-py3-none-any.whl (1.3 MB)
Building wheels for collected packages: xewe-os-tools
  Building wheel for xewe-os-tools (pyproject.toml): started
  Building wheel for xewe-os-tools (pyproject.toml): finished with status 'done'
  Created wheel for xewe-os-tools: filename=xewe_os_tools-3.1.2-py3-none-any.whl size=155566 sha256=5931cb1d50461981fcf173bbabaef3f26cd2c78325dc77cf0e549a162973f69c
  Stored in directory: /tmp/pip-ephem-wheel-cache-hn4won2v/wheels/0a/64/7d/7f9eb638ef60664f1ca6f3c87235609f67c2d77a61b24a9c1f
Successfully built xewe-os-tools
Installing collected packages: pyserial, clang-format, ruff, pygments, pluggy, packaging, iniconfig, pytest, xewe-os-tools
Successfully installed clang-format-23.1.3 iniconfig-2.3.1 packaging-26.3 pluggy-1.7.0 pygments-2.21.0 pyserial-3.5 pytest-9.1.1 ruff-0.17.0 xewe-os-tools-3.1.2
```

#### 3-setup-harness-project (last 20 lines of 3-setup-harness-project.log)

```text
arduino-cli 1.5.1
esp32 core 3.3.12 already installed in ~/.xewe-os/build-tools/arduino15
esptool 5.3.1
core: cloning https://github.com/xewe-labs/xewe-os-core at 3.1.0
modules: using local source ~/todo/xewe-labs/xewe-os-modules/build/release-venv/xewe-os-modules
installed build/modules/src/Buttons  (buttons 3.1.0)
installed build/modules/src/Led  (led 3.1.0)
installed build/modules/src/Wifi  (wifi 3.1.0)
installed build/modules/src/WebInterface  (web-interface 3.1.0)
installed build/modules/src/LedWeb  (led-web 3.1.0)
installed build/modules/src/Pins  (pins 3.1.0)
installed build/modules/src/Time  (time 3.1.0)
installed build/modules/src/Scheduler  (scheduler 3.1.0)
Config.h: added the led, wifi, web-interface, led-web, time block(s)
ArduinoJson: cloning https://github.com/bblanchon/ArduinoJson at v7.4.3
library FastLED: from modules catalogue (3.10.3)
FastLED: cloning https://github.com/FastLED/FastLED at 3.10.3
library WebSockets: from modules catalogue (77024d2723788044ac0715bccd2f42e84bdb2817)
WebSockets: cloning https://github.com/xewe-labs/xewe-led-library-websockets at 77024d2723788044ac0715bccd2f42e84bdb2817
setup complete: arduino-cli 1.5.1, esp32 3.3.12, core 3.1.0, modules 3.0.0 (buttons, led, wifi, web-interface, led-web, pins, time, scheduler)
```

#### 4-setup-harness-alone (last 20 lines of 4-setup-harness-alone.log)

```text
arduino-cli 1.5.1
esp32 core 3.3.12 already installed in ~/.xewe-os/build-tools/arduino15
esptool 5.3.1
core: cloning https://github.com/xewe-labs/xewe-os-core at 3.1.0
modules: using local source ~/todo/xewe-labs/xewe-os-modules/build/release-venv/xewe-os-modules
installed build/modules/src/Buttons  (buttons 3.1.0)
installed build/modules/src/Led  (led 3.1.0)
installed build/modules/src/Wifi  (wifi 3.1.0)
installed build/modules/src/WebInterface  (web-interface 3.1.0)
installed build/modules/src/LedWeb  (led-web 3.1.0)
installed build/modules/src/Pins  (pins 3.1.0)
installed build/modules/src/Time  (time 3.1.0)
installed build/modules/src/Scheduler  (scheduler 3.1.0)
Config.h: added the led, wifi, web-interface, led-web, time block(s)
ArduinoJson: cloning https://github.com/bblanchon/ArduinoJson at v7.4.3
library FastLED: from modules catalogue (3.10.3)
FastLED: cloning https://github.com/FastLED/FastLED at 3.10.3
library WebSockets: from modules catalogue (77024d2723788044ac0715bccd2f42e84bdb2817)
WebSockets: cloning https://github.com/xewe-labs/xewe-led-library-websockets at 77024d2723788044ac0715bccd2f42e84bdb2817
setup complete: arduino-cli 1.5.1, esp32 3.3.12, core 3.1.0, modules 3.0.0 (buttons, led, wifi, web-interface, led-web, pins, time, scheduler)
```

#### 5-validate-validate (last 20 lines of 5-validate-validate.log)

```text
8 modules, 0 errors, 0 warnings
```

#### 7-check-format (last 20 lines of 7-check-format.log)

```text
format: 62 C++ and 18 Python files, 0 would reformat
```

#### 8-check-brand-lint (last 20 lines of 8-check-brand-lint.log)

```text
brand-lint: clean
```

## 5. Tests

### Runs

| Run | Command | Chip | Board | Unit passed | Hardware passed | Compile-only passed | Compiled, not run | Skipped (other) | XFAIL | Failed | Exit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 6-unit-unit | `env XEWE_NO_BOARD=1 build/release-harness/project/build/tools/.venv/bin/python -m xewe --project build/release-harness/project test --unit-only -- '--junitxml=~/todo/xewe-labs/xewe-os-modules/build/release-venv/junit-unit.xml' -o junit_family=xunit1 -o junit_logging=no -ra` | - | - | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 37-board-c6 | `env XEWE_NO_BOARD=1 XEWE_TEST_LED_RESET=1 XEWE_TEST_LED_WEB_HTTP=1 XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1 build/release-harness/project/build/tools/.venv/bin/python -m xewe --project build/release-harness/project test --chip c6 -- -m board -o junit_family=xunit1 -ra '--junitxml=~/todo/xewe-labs/xewe-os-modules/build/release-venv/junit-board-c6.xml'` | c6 | - | 0 | 0 | 1 | 2 | 0 | 0 | 0 | 0 |
| 39-board-s3 | `env XEWE_NO_BOARD=1 XEWE_TEST_LED_RESET=1 XEWE_TEST_LED_WEB_HTTP=1 XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1 build/release-harness/project/build/tools/.venv/bin/python -m xewe --project build/release-harness/project test --chip s3 -- -m board -o junit_family=xunit1 -ra '--junitxml=~/todo/xewe-labs/xewe-os-modules/build/release-venv/junit-board-s3.xml'` | s3 | - | 0 | 0 | 1 | 2 | 0 | 0 | 0 | 0 |
| 40-board-c3 | `env -u XEWE_NO_BOARD XEWE_TEST_LED_RESET=1 XEWE_TEST_LED_WEB_HTTP=1 XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1 build/release-harness/project/build/tools/.venv/bin/python -m xewe --project build/release-harness/project test --chip c3 --require-board -- -m board -o junit_family=xunit1 -ra '--junitxml=~/todo/xewe-labs/xewe-os-modules/build/release-venv/junit-board-c3.xml'` | c3 | sn=AC:27:6E:73:C0:B4 | 0 | 2 | 1 | 0 | 0 | 0 | 0 | 0 |

### Every test

| Test | Kind | Chip | Result | Detail |
|---|---|---|---|---|
| `modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove` | board | c3 | PASS (hardware) | board c3 sn=AC:27:6E:73:C0:B4 fw ecfc3874f13e |
| `modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove` | board | c6 | SKIP (compiled, not run) | no board attached (board access disabled (--no-board / XEWE_NO_BOARD)) |
| `modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove` | board | s3 | SKIP (compiled, not run) | no board attached (board access disabled (--no-board / XEWE_NO_BOARD)) |
| `modules/scheduler/tests/board/test_scheduler.py::test_compiles` | board | c3 | PASS (compile only) | built for c3, no board needed |
| `modules/scheduler/tests/board/test_scheduler.py::test_compiles` | board | c6 | PASS (compile only) | built for c6, no board needed |
| `modules/scheduler/tests/board/test_scheduler.py::test_compiles` | board | s3 | PASS (compile only) | built for s3, no board needed |
| `modules/scheduler/tests/board/test_scheduler.py::test_status` | board | c3 | PASS (hardware) | board c3 sn=AC:27:6E:73:C0:B4 fw ecfc3874f13e |
| `modules/scheduler/tests/board/test_scheduler.py::test_status` | board | c6 | SKIP (compiled, not run) | no board attached (board access disabled (--no-board / XEWE_NO_BOARD)) |
| `modules/scheduler/tests/board/test_scheduler.py::test_status` | board | s3 | SKIP (compiled, not run) | no board attached (board access disabled (--no-board / XEWE_NO_BOARD)) |
| `modules/scheduler/tests/unit/test_scheduler.py::test_add_sample_commands_start_with_dollar` | unit | - | PASS | 0.00 s |
| `modules/scheduler/tests/unit/test_scheduler.py::test_properties_match_source` | unit | - | PASS | 0.00 s |

## 6. Local logs

The record of this run in `releases/3.1.0/logs` (not committed, not published).

| File | Bytes | sha256 |
|---|---|---|
| 1-setup-venv.log | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| 10-build-c3-led-web.log | 829 | `8145ad5dd13466e03c3077751afb3b1c9250c432b8c65391af7e9dddc743a027` |
| 10-build-c3-led-web.meta.json | 1,142 | `ba8560c71422cf93ef20dd5d60470d80e624813021cde843686dc63dc3ea1f89` |
| 11-build-c3-led.log | 692 | `86da7c2d1f47b842db675881b8a4232db2d0558292026551989ae0e596885a5e` |
| 11-build-c3-led.meta.json | 1,083 | `c1592f6ce32ca817627bd49160ab6526f69608b192ed935fe4f4896b06fbb827` |
| 12-build-c3-pins.log | 760 | `2b527c13acc9fe3ae2206e7a7f0c5e382ac8ec8a01b715e4a428a6776b337335` |
| 12-build-c3-pins.meta.json | 1,085 | `12beb65a2b518452cdc69869b20cf0778951ba52ad3d4198493c1a7aae5d983d` |
| 13-build-c3-scheduler.log | 841 | `18e09a0ef5a617014fc5c8e64c7c5e0c9f89b71aff711365d05ac97eca356f0a` |
| 13-build-c3-scheduler.meta.json | 1,124 | `e798987dad6befb190e7d5930a38815aa142a0d2047b4fdadf2cbaa34ec42c78` |
| 14-build-c3-time.log | 722 | `f21283c0c631ad6087a424b426b6d225f9ca05ac2cfdde65a5412e4f635623fc` |
| 14-build-c3-time.meta.json | 1,100 | `234f55dacebab8da892dbb29a72adea38d029e4c9e0711eb886de8c7f404fe7a` |
| 15-build-c3-web-interface.log | 766 | `52ad078d3e9c13216bb740b466c281cebabdb62d173cc18102a9707f3e90bbae` |
| 15-build-c3-web-interface.meta.json | 1,118 | `73655bb844c1ad89b2cc2aa4a41457b22d79ee46a083b0d159f44abb089391a3` |
| 16-build-c3-wifi.log | 697 | `8771682524cc18aeb7ae22e02b91c95e6b46cb217b9022f29e296e0444492d47` |
| 16-build-c3-wifi.meta.json | 1,086 | `b68db4d3f206d38fe643089170e94ed2b7ec762a9989a7c5ca041ede0c0d886d` |
| 17-build-c6-buttons.log | 775 | `f5365703a93514540c08eab36fceca69466ce7270750140c2f4aee6635772f5d` |
| 17-build-c6-buttons.meta.json | 1,091 | `aa471b184a0deb756205f0ddabd004a4e9823eceff37b63879b444422f273913` |
| 18-build-c6-led-web.log | 829 | `688a842d5aa2b270fbcacdc5ee106b36bf24e9b09d274ddc9b4e07a5fcf7a605` |
| 18-build-c6-led-web.meta.json | 1,142 | `f8ab7613ff9a3dadffcad2a6a2f2554a1302f20708ed28e4fa7a7c6a51388464` |
| 19-build-c6-led.log | 692 | `98fcc076a4d8cb0100445ec4823e6e79a68af5283bfa62dc78e7cd75a3791007` |
| 19-build-c6-led.meta.json | 1,083 | `919734b67ed5b1e43f1e5201431f201033d8a575dbf09822c91abd44eea75112` |
| 2-setup-install.log | 3,158 | `3a533d98c959a74b4f60be6a3e32f4c104b720506f4ba3491d9e27779986d024` |
| 20-build-c6-pins.log | 760 | `1810623a53e09b15be27d760f64cd32b00223d6187ef2a5b41a22ccc40ed94fe` |
| 20-build-c6-pins.meta.json | 1,085 | `87a5b69e957dc185aaf8fa7e1676e3b6b1813ff2d783a7bb8122c252ff15e84a` |
| 21-build-c6-scheduler.log | 841 | `b4c7a39ee257038a9bf131527041fd5e79abeb42f7ea7ff2eecb1303eaf162aa` |
| 21-build-c6-scheduler.meta.json | 1,124 | `8f2ad8e9d105e6e6268f8c884a94c04c440f8e05eb0688d675c3068affdca3ea` |
| 22-build-c6-time.log | 722 | `736e3dba7b521731385f04dede3ddb54a787e5144611eb40a43e3cb679d332a6` |
| 22-build-c6-time.meta.json | 1,100 | `1408609c167213845ed3b11104f26d0a6fc91aea0cf09ec4dfbca1fa186859b7` |
| 23-build-c6-web-interface.log | 766 | `30062711541c88db3db05e7158b3e2ce3211ff3eb8ad4c781028cad0d4c829bb` |
| 23-build-c6-web-interface.meta.json | 1,118 | `ba8a1e917a5c168ce9597283042ad9c529ae3092c25201b7f4aa9f60a1caf7dd` |
| 24-build-c6-wifi.log | 697 | `45ebffb55386a10be42a08b2e98357b6261e9249663b1f65cf6a2daec628efab` |
| 24-build-c6-wifi.meta.json | 1,086 | `45c188af2944654d3046c90cbf7747b17cd9e7fbf4c1528c8a0972f7cd9cc54c` |
| 25-build-s3-buttons.log | 775 | `134167e62b07baec04162af8d3c7118993f1149136d0480756631af2e15703e5` |
| 25-build-s3-buttons.meta.json | 1,091 | `810437a3ad31472f2b8ca029c1ceff188b4b4706186aad0d4382816f9c09cfed` |
| 26-build-s3-led-web.log | 829 | `1e2c723da9ffb74f4df5bd9555c096515518853a7df262a6a3abb21e6bc36446` |
| 26-build-s3-led-web.meta.json | 1,142 | `ce8ed109dbb1f72b03793018f71eecb7e2ee924cf126128e69327fcd6dccd261` |
| 27-build-s3-led.log | 692 | `7187b6d6b286d7f56a66a8d8264061d388a5d1a5a96c3d254b3e16ec8051c0db` |
| 27-build-s3-led.meta.json | 1,083 | `b0ced609eec17032b499d97208238e6e515e791f8bab74802969fc30ace4287c` |
| 28-build-s3-pins.log | 760 | `2f2d4f0dd3ece55612ccff41beb4bdaca79828208c05f5875593bd77185766d3` |
| 28-build-s3-pins.meta.json | 1,085 | `536229d75b463198db4c62a95ae92d9d26f7f87b6cb37e2e09e16df1b3a6dc83` |
| 29-build-s3-scheduler.log | 841 | `065326ac909a7489056dec583a042cc40a824c47d802ab3d6d2f000504477d38` |
| 29-build-s3-scheduler.meta.json | 1,124 | `70d1329dfb8cada75002518adea572fd0033d954f962a82cd7f00204f4121217` |
| 3-setup-harness-project.build_config.toml | 1,485 | `4ed17dc4963fdac39b87fbc019e7a35f27525e97c1e0e9426eabdec72dffaddd` |
| 3-setup-harness-project.log | 1,319 | `e6b72d69fc50502160e413110a67ccb5c85c210eb3366dd68e719c51077b66a4` |
| 30-build-s3-time.log | 722 | `dbbf9c94e8b48ab0cc2d0e5ecd4f65f8b15a0aae8bd9b6827ff84e5462a60095` |
| 30-build-s3-time.meta.json | 1,100 | `220bcab008dc5c6040efbf60f94932fdab322cd596d4ef2e25e3c37b260f384a` |
| 31-build-s3-web-interface.log | 765 | `dea7267ab23cb48149e485a3fd0d26bae136b610d925979d5733e013222f0d7d` |
| 31-build-s3-web-interface.meta.json | 1,117 | `7c6eecb948cb64adc87e529a5be7b9fff6f47f332731814381d4e76938fc8aa3` |
| 32-build-s3-wifi.log | 696 | `88f2490b95b353955f0bde01ca05c80a650e52bd753d6873938b2b7c4207e0ad` |
| 32-build-s3-wifi.meta.json | 1,085 | `05db29ee8001c01742b3de37d9b20be3faaf9bc1d20436b8018b1c352aa23655` |
| 33-build-c3-all-modules.log | 383 | `83e308cae5d8684cdd2555cd5b8192ae80a800cad31bf4e1bc8a528a92863fb6` |
| 33-build-c3-all-modules.meta.json | 1,326 | `7d846d7b15c500a11f014410103de7b0164332acb303fa1be7bc69ab13ba8008` |
| 34-flash-c3.log | 213 | `5055a6ca0b3a6774aaebd744ac2af8cb494f78013178a9a04deb46d3b5ffa709` |
| 34-flash-c3.meta.json | 1,326 | `7d846d7b15c500a11f014410103de7b0164332acb303fa1be7bc69ab13ba8008` |
| 35-board-c3.log | 968 | `4dba21e97564b4bfdf8f67804564eba01d58901683595abf642b473ed61e6f70` |
| 35-board-c3.xml | 678 | `886972c9cbfc130b085caded1b1571c03867880986c15056dc5c73384043886d` |
| 36-build-c6-all-modules.log | 383 | `b7b159084bb29f2f226bcb7df5779ff50f320a57f2cdd2afd225b6252ad31872` |
| 36-build-c6-all-modules.meta.json | 1,326 | `7dbbb7f825effcc361ff53773af3e78e2aad79da41dfac6a8326aa191d8c4d61` |
| 37-board-c6.log | 11,214 | `84507e7100cb1ce243e19c56e6ee547a45814e6a02f53ec259b0e02bc3b4a2d1` |
| 37-board-c6.xml | 31,421 | `dcac9d7393533fcb673a1aee384441db8a7987d5a062a472c3464e4faf4b280d` |
| 38-build-s3-all-modules.log | 383 | `189c013ab60620d27fa8651307b21ea4c077d57bc2bd1133031be4f253e4d97a` |
| 38-build-s3-all-modules.meta.json | 1,326 | `2cf92e8e2ac014c7996e38da637f294f190febf11ca2afd6653ab2c3f52329c3` |
| 39-board-s3.log | 11,214 | `6d4965366313b563df0c04cf8934cebacd1ea40f533cad86d2836ff45dffced1` |
| 39-board-s3.xml | 31,421 | `0ea4f67cbcf1249d02cb9a78ed8241f9301ce8088aa23d982fe2f587775d3d5e` |
| 4-setup-harness-alone.build_config.toml | 1,485 | `4ed17dc4963fdac39b87fbc019e7a35f27525e97c1e0e9426eabdec72dffaddd` |
| 4-setup-harness-alone.log | 1,319 | `e6b72d69fc50502160e413110a67ccb5c85c210eb3366dd68e719c51077b66a4` |
| 40-board-c3.log | 2,009 | `930c5ae1dca4d434d768b508404b7c179436474976f7421ccc6100fd50268e16` |
| 40-board-c3.xml | 28,222 | `48b84ea43a2d74ce11190eea9319697cd83379d9f0fb333d2bc72f897fc47750` |
| 5-validate-validate.log | 32 | `390f25d997a1ca5b0e0fd5ae8cd9f460880e61a8481eb0c1dc46d13c9d5df7c8` |
| 6-unit-unit.log | 3,571 | `b78558a6678dad15c234c5949a57dfb651b65282aaf6ff6adccf48226a84686b` |
| 6-unit-unit.xml | 28,123 | `40492db0694b502c84c58cc77577bea4ff89c4fed04a87083c33cce42d3fff74` |
| 7-check-format.log | 53 | `c47c6bf5005242d33e8b250209c76bf721dd6c801bae77ddcc91528a6c22abd2` |
| 8-check-brand-lint.log | 18 | `bda010d633eeae56194d6295464ea2c0e0977f69156569e7282df247d0373ae7` |
| 9-build-c3-buttons.log | 775 | `21a85302fd88e9f6b368ce58dd61bda1ac76bf28714a913c9d7d3fd403678b48` |
| 9-build-c3-buttons.meta.json | 1,091 | `f5d43e89c4ebaf152a0798fb0c47d966bc2eb54ae2d01cc1b106b72ed42fbf00` |
| run.json | 59,155 | `df5792d0523e117a2750ae4a4dd7bcb69e5d29bc2ca07359f86b13f382204917` |

## 7. Not covered

- Chips without a hardware run: c6, s3.
- Skipped, by reason:
  - compiled, not run: no board attached (board access disabled (--no-board / XEWE_NO_BOARD)) (4): `modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove [c6]`, `modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove [s3]`, `modules/scheduler/tests/board/test_scheduler.py::test_status [c6]`, and 1 more
- Manual checks: tests/MANUAL.md and the BOARD-TEST-PLAN ids are not part of this card; the release notes name what was checked by hand.

## 8. Retries

| Earlier attempt | Attempt | Exit | Time | Log |
|---|---|---|---|---|
| 35-board-c3 | 1 | 1 | 116.0 s | 35-board-c3.log |

<!-- xewe-card-data {"schema":1,"tree":"sha256:30e2897ee6bfdfe92f4135645a9b20e604beb16b37b5526434dc9be746476128","run":"20261020T165327Z-b509","tests":[["modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove","board","c3","PASS (hardware)"],["modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove","board","c6","SKIP (compiled, not run)"],["modules/scheduler/tests/board/test_scheduler.py::test_add_then_remove","board","s3","SKIP (compiled, not run)"],["modules/scheduler/tests/board/test_scheduler.py::test_compiles","board","c3","PASS (compile only)"],["modules/scheduler/tests/board/test_scheduler.py::test_compiles","board","c6","PASS (compile only)"],["modules/scheduler/tests/board/test_scheduler.py::test_compiles","board","s3","PASS (compile only)"],["modules/scheduler/tests/board/test_scheduler.py::test_status","board","c3","PASS (hardware)"],["modules/scheduler/tests/board/test_scheduler.py::test_status","board","c6","SKIP (compiled, not run)"],["modules/scheduler/tests/board/test_scheduler.py::test_status","board","s3","SKIP (compiled, not run)"],["modules/scheduler/tests/unit/test_scheduler.py::test_add_sample_commands_start_with_dollar","unit","-","PASS"],["modules/scheduler/tests/unit/test_scheduler.py::test_properties_match_source","unit","-","PASS"]],"images":{"c3":{"sketch_size":1508041,"static_ram":50168,"config":{}},"c6":{"sketch_size":1550326,"static_ram":57100,"config":{}},"s3":{"sketch_size":1362864,"static_ram":59784,"config":{}}},"exclude":["releases/","modules/*/releases","releases/3.1.0/logs/"],"logs":{"1-setup-venv.log":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","10-build-c3-led-web.log":"8145ad5dd13466e03c3077751afb3b1c9250c432b8c65391af7e9dddc743a027","10-build-c3-led-web.meta.json":"ba8560c71422cf93ef20dd5d60470d80e624813021cde843686dc63dc3ea1f89","11-build-c3-led.log":"86da7c2d1f47b842db675881b8a4232db2d0558292026551989ae0e596885a5e","11-build-c3-led.meta.json":"c1592f6ce32ca817627bd49160ab6526f69608b192ed935fe4f4896b06fbb827","12-build-c3-pins.log":"2b527c13acc9fe3ae2206e7a7f0c5e382ac8ec8a01b715e4a428a6776b337335","12-build-c3-pins.meta.json":"12beb65a2b518452cdc69869b20cf0778951ba52ad3d4198493c1a7aae5d983d","13-build-c3-scheduler.log":"18e09a0ef5a617014fc5c8e64c7c5e0c9f89b71aff711365d05ac97eca356f0a","13-build-c3-scheduler.meta.json":"e798987dad6befb190e7d5930a38815aa142a0d2047b4fdadf2cbaa34ec42c78","14-build-c3-time.log":"f21283c0c631ad6087a424b426b6d225f9ca05ac2cfdde65a5412e4f635623fc","14-build-c3-time.meta.json":"234f55dacebab8da892dbb29a72adea38d029e4c9e0711eb886de8c7f404fe7a","15-build-c3-web-interface.log":"52ad078d3e9c13216bb740b466c281cebabdb62d173cc18102a9707f3e90bbae","15-build-c3-web-interface.meta.json":"73655bb844c1ad89b2cc2aa4a41457b22d79ee46a083b0d159f44abb089391a3","16-build-c3-wifi.log":"8771682524cc18aeb7ae22e02b91c95e6b46cb217b9022f29e296e0444492d47","16-build-c3-wifi.meta.json":"b68db4d3f206d38fe643089170e94ed2b7ec762a9989a7c5ca041ede0c0d886d","17-build-c6-buttons.log":"f5365703a93514540c08eab36fceca69466ce7270750140c2f4aee6635772f5d","17-build-c6-buttons.meta.json":"aa471b184a0deb756205f0ddabd004a4e9823eceff37b63879b444422f273913","18-build-c6-led-web.log":"688a842d5aa2b270fbcacdc5ee106b36bf24e9b09d274ddc9b4e07a5fcf7a605","18-build-c6-led-web.meta.json":"f8ab7613ff9a3dadffcad2a6a2f2554a1302f20708ed28e4fa7a7c6a51388464","19-build-c6-led.log":"98fcc076a4d8cb0100445ec4823e6e79a68af5283bfa62dc78e7cd75a3791007","19-build-c6-led.meta.json":"919734b67ed5b1e43f1e5201431f201033d8a575dbf09822c91abd44eea75112","2-setup-install.log":"3a533d98c959a74b4f60be6a3e32f4c104b720506f4ba3491d9e27779986d024","20-build-c6-pins.log":"1810623a53e09b15be27d760f64cd32b00223d6187ef2a5b41a22ccc40ed94fe","20-build-c6-pins.meta.json":"87a5b69e957dc185aaf8fa7e1676e3b6b1813ff2d783a7bb8122c252ff15e84a","21-build-c6-scheduler.log":"b4c7a39ee257038a9bf131527041fd5e79abeb42f7ea7ff2eecb1303eaf162aa","21-build-c6-scheduler.meta.json":"8f2ad8e9d105e6e6268f8c884a94c04c440f8e05eb0688d675c3068affdca3ea","22-build-c6-time.log":"736e3dba7b521731385f04dede3ddb54a787e5144611eb40a43e3cb679d332a6","22-build-c6-time.meta.json":"1408609c167213845ed3b11104f26d0a6fc91aea0cf09ec4dfbca1fa186859b7","23-build-c6-web-interface.log":"30062711541c88db3db05e7158b3e2ce3211ff3eb8ad4c781028cad0d4c829bb","23-build-c6-web-interface.meta.json":"ba8a1e917a5c168ce9597283042ad9c529ae3092c25201b7f4aa9f60a1caf7dd","24-build-c6-wifi.log":"45ebffb55386a10be42a08b2e98357b6261e9249663b1f65cf6a2daec628efab","24-build-c6-wifi.meta.json":"45c188af2944654d3046c90cbf7747b17cd9e7fbf4c1528c8a0972f7cd9cc54c","25-build-s3-buttons.log":"134167e62b07baec04162af8d3c7118993f1149136d0480756631af2e15703e5","25-build-s3-buttons.meta.json":"810437a3ad31472f2b8ca029c1ceff188b4b4706186aad0d4382816f9c09cfed","26-build-s3-led-web.log":"1e2c723da9ffb74f4df5bd9555c096515518853a7df262a6a3abb21e6bc36446","26-build-s3-led-web.meta.json":"ce8ed109dbb1f72b03793018f71eecb7e2ee924cf126128e69327fcd6dccd261","27-build-s3-led.log":"7187b6d6b286d7f56a66a8d8264061d388a5d1a5a96c3d254b3e16ec8051c0db","27-build-s3-led.meta.json":"b0ced609eec17032b499d97208238e6e515e791f8bab74802969fc30ace4287c","28-build-s3-pins.log":"2f2d4f0dd3ece55612ccff41beb4bdaca79828208c05f5875593bd77185766d3","28-build-s3-pins.meta.json":"536229d75b463198db4c62a95ae92d9d26f7f87b6cb37e2e09e16df1b3a6dc83","29-build-s3-scheduler.log":"065326ac909a7489056dec583a042cc40a824c47d802ab3d6d2f000504477d38","29-build-s3-scheduler.meta.json":"70d1329dfb8cada75002518adea572fd0033d954f962a82cd7f00204f4121217","3-setup-harness-project.build_config.toml":"4ed17dc4963fdac39b87fbc019e7a35f27525e97c1e0e9426eabdec72dffaddd","3-setup-harness-project.log":"e6b72d69fc50502160e413110a67ccb5c85c210eb3366dd68e719c51077b66a4","30-build-s3-time.log":"dbbf9c94e8b48ab0cc2d0e5ecd4f65f8b15a0aae8bd9b6827ff84e5462a60095","30-build-s3-time.meta.json":"220bcab008dc5c6040efbf60f94932fdab322cd596d4ef2e25e3c37b260f384a","31-build-s3-web-interface.log":"dea7267ab23cb48149e485a3fd0d26bae136b610d925979d5733e013222f0d7d","31-build-s3-web-interface.meta.json":"7c6eecb948cb64adc87e529a5be7b9fff6f47f332731814381d4e76938fc8aa3","32-build-s3-wifi.log":"88f2490b95b353955f0bde01ca05c80a650e52bd753d6873938b2b7c4207e0ad","32-build-s3-wifi.meta.json":"05db29ee8001c01742b3de37d9b20be3faaf9bc1d20436b8018b1c352aa23655","33-build-c3-all-modules.log":"83e308cae5d8684cdd2555cd5b8192ae80a800cad31bf4e1bc8a528a92863fb6","33-build-c3-all-modules.meta.json":"7d846d7b15c500a11f014410103de7b0164332acb303fa1be7bc69ab13ba8008","34-flash-c3.log":"5055a6ca0b3a6774aaebd744ac2af8cb494f78013178a9a04deb46d3b5ffa709","34-flash-c3.meta.json":"7d846d7b15c500a11f014410103de7b0164332acb303fa1be7bc69ab13ba8008","35-board-c3.log":"4dba21e97564b4bfdf8f67804564eba01d58901683595abf642b473ed61e6f70","35-board-c3.xml":"886972c9cbfc130b085caded1b1571c03867880986c15056dc5c73384043886d","36-build-c6-all-modules.log":"b7b159084bb29f2f226bcb7df5779ff50f320a57f2cdd2afd225b6252ad31872","36-build-c6-all-modules.meta.json":"7dbbb7f825effcc361ff53773af3e78e2aad79da41dfac6a8326aa191d8c4d61","37-board-c6.log":"84507e7100cb1ce243e19c56e6ee547a45814e6a02f53ec259b0e02bc3b4a2d1","37-board-c6.xml":"dcac9d7393533fcb673a1aee384441db8a7987d5a062a472c3464e4faf4b280d","38-build-s3-all-modules.log":"189c013ab60620d27fa8651307b21ea4c077d57bc2bd1133031be4f253e4d97a","38-build-s3-all-modules.meta.json":"2cf92e8e2ac014c7996e38da637f294f190febf11ca2afd6653ab2c3f52329c3","39-board-s3.log":"6d4965366313b563df0c04cf8934cebacd1ea40f533cad86d2836ff45dffced1","39-board-s3.xml":"0ea4f67cbcf1249d02cb9a78ed8241f9301ce8088aa23d982fe2f587775d3d5e","4-setup-harness-alone.build_config.toml":"4ed17dc4963fdac39b87fbc019e7a35f27525e97c1e0e9426eabdec72dffaddd","4-setup-harness-alone.log":"e6b72d69fc50502160e413110a67ccb5c85c210eb3366dd68e719c51077b66a4","40-board-c3.log":"930c5ae1dca4d434d768b508404b7c179436474976f7421ccc6100fd50268e16","40-board-c3.xml":"48b84ea43a2d74ce11190eea9319697cd83379d9f0fb333d2bc72f897fc47750","5-validate-validate.log":"390f25d997a1ca5b0e0fd5ae8cd9f460880e61a8481eb0c1dc46d13c9d5df7c8","6-unit-unit.log":"b78558a6678dad15c234c5949a57dfb651b65282aaf6ff6adccf48226a84686b","6-unit-unit.xml":"40492db0694b502c84c58cc77577bea4ff89c4fed04a87083c33cce42d3fff74","7-check-format.log":"c47c6bf5005242d33e8b250209c76bf721dd6c801bae77ddcc91528a6c22abd2","8-check-brand-lint.log":"bda010d633eeae56194d6295464ea2c0e0977f69156569e7282df247d0373ae7","9-build-c3-buttons.log":"21a85302fd88e9f6b368ce58dd61bda1ac76bf28714a913c9d7d3fd403678b48","9-build-c3-buttons.meta.json":"f5d43e89c4ebaf152a0798fb0c47d966bc2eb54ae2d01cc1b106b72ed42fbf00","run.json":"df5792d0523e117a2750ae4a4dd7bcb69e5d29bc2ca07359f86b13f382204917"},"repo":"xewe-labs/xewe-os-modules","version":"3.1.0","commit":"4ebe734458e7927d80cb94cafeeae76a6a3451e3","chips":["c3","c6","s3"],"hardware":["c3"],"require":["setup","validate","unit","build","board"],"only":["modules/scheduler/"],"card_sha256":"58953119fcbe3f362e39b67a984ce493dc4ab745dca738e4d1b5325c97ab424a"} -->
