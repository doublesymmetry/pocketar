## Application arguments

`PocketARArgs.read()` is the addon argument helper. It returns a Dictionary with
`args: PackedStringArray` and `error: String`. Desktop and Android use
`OS.get_cmdline_user_args()`. Debug iOS builds read the `POCKETAR_ARGS` JSON array
when present; malformed JSON/non-string elements return a transport error. Release
iOS ignores that environment variable. Harness-specific validation and capture
cleanup stay in `harness/ar/harness_args.gd`.

Android accepts two string-array intent extras in debuggable builds only, guarded
by `ApplicationInfo.FLAG_DEBUGGABLE`: `pocketar_harness_args` retains the harness
allowlist, and `pocketar_app_args` carries application-owned flags. Both use the
existing comma/backslash escaping and are appended after one `--`; app-only
launches work too. Every app entry must be nonblank, have no leading/trailing
whitespace, and must not start with `--`; game flags use a single dash. A malformed
app extra is logged and discarded in full while valid harness arguments survive.
Applications validate the remaining flag names and values. Invalid harness extras
still reject the launch arguments. Release builds ignore both extras.
