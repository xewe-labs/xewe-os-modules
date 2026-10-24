# XeWeCore conventions

Modules follow the conventions XeWeCore introduced in 2.1. `doc/CONTRACT.md` section 3.1 has
the detail. XeWeCore's own reference is listed in DOCS_AND_COMMENTS.md.

## Settings table

Plain settings live in a `static constexpr xewe::SettingDef` table built with
`xewe::setting<&Class::member>("key", ...)` rows and returned by `settings()` as `{table, this}`.
Never write hand-made NVS reads or writes for them. The core loads the table before
`begin_routines_required` and owns `$<id> set|get|schema`. Apply a change in
`on_setting_changed(def)`, which also runs while the module is disabled and is not called by
the boot load. Mark credentials `SECRET` and rows that apply after a reboot `RESTART`. A module
that already owns `set` forwards non-numeric first arguments to `apply_setting`, and older alias
commands call `apply_setting` too.

## Values that are not rows

`schema_extra` reports them as rows with a `"group"` and a `"set"` hint (scheduler blocks,
button mappings). Without a table the core registers no `schema` command, so scheduler and
buttons register their own.

## Listeners

Change events go through `xewe::ListenerSet<Iface, N>`, which is fixed size and uses no heap.
Every event carries `const void* origin` (`nullptr` from the CLI), so a listener can drop its
own echo. Callbacks run in the caller's task, outside any render lock, so keep them short.

## Pin claims

Claim every driven GPIO with `xewe::pins::claim(gpio, id.c_str())` before configuring it, and
`release` it when done: on `remove`, a pin change or `reset`. A refused claim aborts the
operation, and the core prints the conflict. `$pins claims` shows the registry.

## Stored blobs

FlexData blobs carry `schema`. Accept a blob only when `has("schema")` and the value matches.
Never reinterpret or overwrite another layout at boot.

## Colours

Hex colours use the core's parser in `XeWeCore/Utils/Color.h`. No module keeps its own copy.

## Module notes

- wifi's `loop()` waits for the network while disconnected, by design (`doc/CONTRACT.md`
  section 6). No other module may block.
- web-interface has no authentication: every command is reachable over HTTP on the local
  network.
- The scheduler's id is `schedule`, and its member for the time dependency is `time_module`.
