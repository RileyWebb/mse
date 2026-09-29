# Credits

MSE is by Riley Webb and is released under the MIT Licence (see LICENCE).

MSE is built on the work of the projects below. Each is used under its own licence, and the full licence text ships with each project's source. CMake downloads these projects while it configures, unless they are marked as included in this repository.

## Libraries

- [SDL3](https://github.com/libsdl-org/SDL): Sam Lantinga and contributors. zlib License. Window, input, audio and GPU.
- [Dear ImGui](https://github.com/ocornut/imgui): Omar Cornut and contributors. MIT License.
- [cimgui](https://github.com/cimgui/cimgui): cimgui contributors. MIT License. C bindings for Dear ImGui.
- [ImPlot](https://github.com/epezent/implot): Evan Pezent. MIT License.
- [cimplot](https://github.com/cimgui/cimplot): cimgui contributors. MIT License. C bindings for ImPlot.
- [FreeType](https://freetype.org): The FreeType Project. FreeType License (FTL). Font rendering for Dear ImGui.
- [LuaJIT](https://luajit.org): Mike Pall. MIT License.
- [luajit-cmake](https://github.com/zhaozg/luajit-cmake): zhaozg. MIT License. Build scripts for LuaJIT.
- [LuaJIT-ImGui](https://github.com/sonoro1234/LuaJIT-ImGui): Victor Bombi. MIT License. Lua bindings for Dear ImGui. The ImVec2/ImVec4 metatypes in `frontend/lua/imgui_prelude.lua` are adapted from it.
- [LuaSocket](https://github.com/lunarmodules/luasocket): Diego Nehab and contributors. MIT License.
- [LuaSec](https://github.com/lunarmodules/luasec): Bruno Silvestre. MIT License.
- [LibreSSL](https://www.libressl.org): The OpenBSD Project. ISC License, with code under the original OpenSSL and SSLeay licences. TLS for LuaSec.
- [SQLite](https://sqlite.org): Public domain. Built from the [amalgamation packaged by Amir Zamani](https://github.com/azadkuh/sqlite-amalgamation) (BSD 3-Clause License).
- [minizip-ng](https://github.com/zlib-ng/minizip-ng): Nathan Moinvaziri and contributors. zlib License.
- [zlib-ng](https://github.com/zlib-ng/zlib-ng): zlib-ng contributors. zlib License. Downloaded by minizip-ng.
- [stb](https://github.com/nothings/stb): Sean Barrett. MIT License or public domain, at your choice.
- [json.lua](https://github.com/rxi/json.lua): rxi. MIT License.
- [md5.lua](https://github.com/kikito/md5.lua): Enrique García Cota. MIT License.

## Code adapted into this repository

- **`frontend/src/cimgui_markdown.c`, `cimgui_markdown.h`**: based on [imgui_markdown](https://github.com/enkisoftware/imgui_markdown) by Juliette Foucaut and Doug Binks. zlib License.
- **`frontend/src/cimgui_hex.c`**: based on [imgui_hex_editor](https://github.com/Teselka/imgui_hex_editor) by Teselka. MIT License.

## Fonts (included in `data/fonts/`)

- [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono): The JetBrains Mono Project Authors. SIL Open Font License 1.1.
- [Roboto Mono](https://github.com/googlefonts/roboto-classic): The Roboto Project Authors. SIL Open Font License 1.1.

Both fonts are the [Nerd Fonts](https://www.nerdfonts.com) patched versions (Ryan L McIntyre, MIT License), which add the icon glyphs the interface uses. The icon sets inside keep their own licences.

## Backends

Each backend credits its own dependencies and resources in its own folder:

- **cNES**: [backends/cnes/CREDITS.md](https://github.com/RileyWebb/mse/blob/master/backends/cnes/CREDITS.md)

The SDL logo shown in the README's badge belongs to the SDL project.
