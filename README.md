PICO-8 to TIC-80 cartridge converter
====================================

[Online converter](https://bztsrc.gitlab.io/p8totic)

This is a small tool that converts the proprietary [PICO-8](https://www.lexaloffle.com/pico-8.php) fantasy console's
cartriges (in both textual `.p8` and binary `.p8.png` format) into the Free and Open Source [TIC-80](https://tic80.com)
console's [.tic](https://github.com/nesbox/TIC-80/wiki/.tic-File-Format) cartridge format, and tries to be feature complete
while doing so.

Features
--------

- Cover image (cartridge label, centered on the 240 x 136 screen)
- Sprites (all 256)
- Map (the map is expanded from 128 x 64 to 240 x 136)
- Lua code (with syntax fixer, API replacer and an additional helper [PICO-8 wrapper for TIC-80](https://github.com/musurca/pico2tic) Lua library)
- Palette, the standard PICO-8 palette is added to the cartridge (TIC-80 supports multiple, modifiable palettes)

TODO: sound effects and music are loaded, but not saved yet. Contributions from someone familiar with the TIC-80 sfx and music
in-memory layout are welcome!

Compilation
-----------

Just run `make` in the [src](https://gitlab.com/bztsrc/p8totic/-/tree/main/src) directory, it is [suckless](https://suckless.org).
Only needs emscripten's `emcc` and `gcc` to compile.

- `make wasm` if you only want to compile the WebAssembly version (the required boilerplate html is in the [public](https://gitlab.com/bztsrc/p8totic/-/tree/main/public) directory).
- `make cli` if you only want to compile the command line version (totally dependency-free, should work on any POSIX system).

License
-------

This converter tool is licensed under the terms of [MIT](LICENSE) license, same as the [TIC-80's license](https://github.com/nesbox/TIC-80/blob/main/LICENSE).

Cheers,
bzt
