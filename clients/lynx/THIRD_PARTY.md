# cc65 Lynx graphics driver

`src/lynx_tgi.s` is a modified copy of cc65's
`libsrc/lynx/tgi/lynx-160-102-16.s` at commit `f54780cfb`, originally by
Karri Kaksonen (2004). The cc65 license is preserved in the source and in
[`third_party/cc65-LICENSE.txt`](third_party/cc65-LICENSE.txt).

Local changes restore the text scanline fill byte, align graphics buffers
and control blocks, and export their addresses for build checks. The driver
is linked into the game; the installed compiler/library are not modified.

# FujiRealm Lynx linker layout

`src/lynx.cfg` was adapted from `fujirealm-game-demo/lynx-client/src/lynx.cfg`.

MIT License

Copyright (c) 2026 mozzwald

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
