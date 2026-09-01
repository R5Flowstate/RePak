# Oodle is not included

Pak-level Kraken compression uses Oodle, which is proprietary middleware owned
by Epic Games Tools LLC and is not ours to redistribute. Nothing here is
vendored and nothing is linked statically.

`src/utils/oodle.cpp` resolves every entry point at runtime from an oo2core
DLL. It uses `OODLE_DLL` if that environment variable is set, otherwise the
first of `oo2core_9_win64.dll` ... `oo2core_5_win64.dll` that loads. Place one
next to `repak.exe`, or point `OODLE_DLL` at one.

Without a DLL, `-compress <pak> oodle` and `-decompress` of an Oodle pak warn
and fail; every other build path is unaffected.

`OODLE_COMPRESSOR` overrides the codec (Kraken is 8, Leviathan 13).
