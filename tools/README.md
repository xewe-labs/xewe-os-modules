# tools/

`validate.py` checks every module under `modules/` against the contract and writes the module
index `doc/modules.md`. It runs the module rules of xewe-os-tools first, then this repository's
own rules, listed in [doc/CONTRACT.md](../doc/CONTRACT.md) section 5. `test_validate.py` tests
its `config` rule with one fixture module per finding.

`validate.py` needs a Python with xewe-os-tools installed, which is the venv of an xewe-os
project used as the harness. Without it, it exits 3.

```sh
<harness>/build/tools/.venv/bin/python tools/validate.py --harness <harness>
<harness>/build/tools/.venv/bin/python tools/validate.py --write-index
python -m pytest tools/test_validate.py
```

Run `--write-index` after any `module.properties` change, then validate again: the `index` rule
fails while `doc/modules.md` is out of date. The test file needs only pytest.
