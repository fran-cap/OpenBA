# third_party/wmadec — the WMA v2 decoder, extracted from FFmpeg

Every sound Banished ships is xWMA (`WAVE_FORMAT_WMAUDIO2`, 0x0161, made by the modkit's
`xWMAEncode.exe`), and SDL3 decodes no WMA. Phase D.4 Q5 (the user, 2026-10-10): extract
FFmpeg's WMA v2 decoder, LGPL, and decode at resource load (a SoundBuffer) or chunk-ahead on
the Stream worker (the music), never on the audio callback. This directory is that decoder
and nothing else: no FFmpeg build system, no assembly, no SIMD, portable C only.
The game's tree sees one header, `include/wmav2_decoder.h`; `src/engine/audio/xwma_decode.{h,cpp}`
is the xWMA layer over it.

## Source

| | |
|---|---|
| repository | https://github.com/FFmpeg/FFmpeg (mirror of https://git.ffmpeg.org/ffmpeg.git) |
| tag | **`n8.1.3`** (FFmpeg 8.1.3, the current stable release on 2026-10-10) |
| tag object | `23151b11c75aa44d9ab8db796a53c76acf00f6c0` |
| commit | `1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7` (2026-09-20) |
| fetched | `git clone --depth 1 --branch n8.1.3 --filter=blob:none --sparse`, then copied by hand |

## Layout

- `ffmpeg/` — **verbatim** upstream files at their upstream paths (`libavcodec/`, `libavutil/`).
  Not one byte is changed; the sha256 of each is below and equals the file at the tag.
- `shim/` — the pieces written here (not FFmpeg files) that let the verbatim sources compile
  without the rest of FFmpeg. Each file says what it stands in for.
- `include/wmav2_decoder.h` — the C API the game uses.
- `COPYING.LGPLv2.1` — the licence, verbatim from the tag.

### Verbatim files (50)

| upstream path | bytes | sha256 |
|---|---|---|
| `libavcodec/defs.h` | 13333 | `f1d391ac2417702b59b7a4b6ae3927eea8c8b5e9858ae9ade6fdd009a60bd9ce` |
| `libavcodec/get_bits.h` | 22488 | `95b0adcce85eadad9b98719658c049f4fe00bac2846ec27a2309a17bc03033c4` |
| `libavcodec/mathops.h` | 6139 | `f0adec2522b8a373cd8cecb5dbc492800a0aa5f05b915481d321cde3ba3cad0f` |
| `libavcodec/put_bits.h` | 11856 | `28684527083ea3b70539b5f2258a08c07833a63b1433a2de11fa22816b6e52eb` |
| `libavcodec/sinewin.c` | 815 | `515494ac041df33d492da0ed3e0cd99d7953db608106141c33a3f46bf44c6101` |
| `libavcodec/sinewin.h` | 1684 | `b486c555871a4d098500ed0f389dff5f5cb07c17df8c70c8c155b5bd3f15f5f9` |
| `libavcodec/sinewin_tablegen.h` | 3366 | `a2defd94fb60f2c6b838e51219afedf6a0991f393fccae6dd9d205a60b5c78de` |
| `libavcodec/vlc.c` | 22329 | `8b44e56d86e0a781aac29aff5fe54e8aec82e923477358e439d619698806f82c` |
| `libavcodec/vlc.h` | 15240 | `5f0a2667bf1942356498c99b2c9433d4365ad1416174864541b1809bd9609132` |
| `libavcodec/wma.c` | 16243 | `b97f11b2dc634c969650dacbae361098dcb5331df0e2164dee70c88962a9bd7e` |
| `libavcodec/wma.h` | 6448 | `7273921bafb32d258b722fe7749d3dafe779fc752fa3e527dd2113064dc56dc0` |
| `libavcodec/wma_common.c` | 1883 | `ca2fd90adcd464ca3fe9d75051034a888108ed7fe31e1afb5e6c7ae1781239b7` |
| `libavcodec/wma_common.h` | 1021 | `77a8e21df2c9b569224c0d2166bb4a8711859902ebcb1152bae53a65337bf2cd` |
| `libavcodec/wma_freqs.c` | 1036 | `c8992e806736dd708015c2121bf1773e4c4bbf3201840cbdf7f6c5f3754ee46c` |
| `libavcodec/wma_freqs.h` | 925 | `aeef407a065e961f18176fb17918c403f69686e6eb84d717bd688b1511789a56` |
| `libavcodec/wmadata.h` | 73786 | `e5b8583007d92d5309078c97a319bac42d2fe33617814bfe8bb88ae9724d6bbf` |
| `libavcodec/wmadec.c` | 36478 | `a57df1c80fc15edb41bd13678c77be52b0e0110f82eab61775672b84e0983a66` |
| `libavutil/attributes.h` | 6410 | `0d3c996c473cc9576ed818332ee3ee65eb057afb4654d91678841cfde78b34ba` |
| `libavutil/attributes_internal.h` | 1794 | `3cc99d4b6f9ca40913bfe539a300ddd84ec9cf15e0fd51d2d7a28405d9dbe8b2` |
| `libavutil/avassert.h` | 4015 | `986182bd395e6559ec1a20243e540018ac99a64f1cd148f203ab6fbc4878f3ac` |
| `libavutil/avstring.h` | 14962 | `630d231013e996b10b2c9aec98c569f8f88dc53a425491d435d868172a53ecb0` |
| `libavutil/bprint.h` | 8818 | `5e3800d797fb8f67ffa7917420d3710c759afaca6f84b655fb64bf4dfa4650d2` |
| `libavutil/bswap.h` | 2788 | `1d985324139f195d3247c3bb59aa7976c074fbaaf85b8b783f88c178c59f48d3` |
| `libavutil/common.h` | 17601 | `c882e2a3e53721474e1370523c555af5315bd6d23c19998bed9f0b5b55c3f99c` |
| `libavutil/cpu.h` | 7050 | `fdab69a7e8820ae44cc94e91dd6a03fe796dbcb7e79f1b291a5a45e04505421e` |
| `libavutil/error.h` | 5555 | `bcf4f7e69c7e0d658ad6e81611810f7cf1f0b8334ebe948d27e518c459e4104c` |
| `libavutil/ffmath.h` | 2073 | `522d6f0b024ebd05a1f4868439a5513321b45b54e1cae387b430da2acefe66ff` |
| `libavutil/float_dsp.c` | 4604 | `349126b23468d502bfa65bb31710600a8e5eb614004c2bcfd5be8545a58fe3d8` |
| `libavutil/float_dsp.h` | 8898 | `e93703c6140f2e69312ed404003b024558fc37a5d40af5e4829a860707a254c7` |
| `libavutil/float_scalarproduct.c` | 1031 | `1f80d292f022154af43f9eb16e52476b91c45dc6ff03f76a623b25d299d9cfcc` |
| `libavutil/internal.h` | 4740 | `7b248f81beabb881edbcc4648d4bc0c64c22f12f8b5199091e7e140606cdce78` |
| `libavutil/intfloat.h` | 1726 | `3a29e4eebc8c269cfd867b96de91d8231773d392c12a8820e46eaba96d2b4ca1` |
| `libavutil/intmath.h` | 4147 | `25f09b4e16dd8edd21cba932f1503d5bc361bd5a6b3993e3cdd82b747128ab53` |
| `libavutil/intreadwrite.h` | 19426 | `6e9c404e70f71a02208af81910434e47c501cbe1b0a80116e731cb673f98c734` |
| `libavutil/libm.h` | 14360 | `e7f4417fba09554b12fb0cdb36ce72b7d24b2d2bfb9865b2bde1056ad0f72131` |
| `libavutil/log.h` | 14088 | `b8ef46be6210baadb3ad61eb72ee48e7dd7d6be39c17c3a8bbc8c81ba9e26110` |
| `libavutil/macros.h` | 2304 | `b63b3a268b096f0eed1e91b821714cff334e5dc5bb34365148704393ae15321e` |
| `libavutil/mathematics.h` | 9565 | `e85c71fb77b0fe7823a88db904982e2a9b953e806ced0714fe6cc6b8a0951469` |
| `libavutil/mem.h` | 20457 | `bf9a1d8c218c38b333efb16be076c5778d31d66a0691bdd6175f171c08283d7c` |
| `libavutil/mem_internal.h` | 4399 | `cfa03ed880bba87fac53e899e109c0b3080831f8a007c436761c7a1ac35084d6` |
| `libavutil/qsort.h` | 4315 | `dd90277c1afd4aaaadabecc3524e417829fcab5324a2149a338f5eb5eda0754c` |
| `libavutil/rational.h` | 6287 | `f4fb850924b414f9ca35579df4e50d6f406734eb9735bbfbf388ae22ec5a0a9c` |
| `libavutil/reverse.c` | 2187 | `1e7908e98aab981c801d6b016a1417d505c998ec8e91a8e1bace6b44a8813c85` |
| `libavutil/reverse.h` | 974 | `7021f8d31b74233859d159ef8108ebe19d2e5e30c2025a728f23009e3233cc9e` |
| `libavutil/tx.c` | 29484 | `591dda843b5dc47876b89227c4e2e360bbb1fa4e782aa56071cc3c5783d5a6ad` |
| `libavutil/tx.h` | 7141 | `a1c5c309f493cc22f4637e67696581be676cd33fdcaa0661a01f5bb4345c900a` |
| `libavutil/tx_float.c` | 827 | `52f854b317d0e4fc8360332a0b1a478af6adb1c8a5a0bd4c6c6af880106ce75c` |
| `libavutil/tx_priv.h` | 17874 | `c3db88b6eb58c8837bfcf3992872376244142d155efe1b03a8b0fe83825434ac` |
| `libavutil/tx_template.c` | 90636 | `b3c4becdffefd82321069775b80bb542e577fb6ad229ea6bda3570d98d76e130` |
| `libavutil/version.h` | 4479 | `bf4a4d1d63f1590bd70a780492405e41f2518078821e8d866b2d4a9278818e6c` |

`libavutil/tx_template.c` is not compiled on its own: `tx_float.c` includes it.
`libavcodec/aactab.c` (sha256 `dc26f9316026cd807617db09c72424ae2b19b07a1d9c19a45cc05ab9617a32ce`)
is NOT vendored; the two tables WMA uses from it are copied into `shim/aac_scalefactor_tables.c`.

### Shim files (written for this extract)

| file | stands in for | what it does |
|---|---|---|
| `shim/config.h` | `configure`'s generated `config.h` | `ARCH_*` 0, no asm / inline asm / SIMD, `HAVE_THREADS` 0, `CONFIG_SMALL` 1 (drops `tx.c`'s debug printer and codelet names), `CONFIG_SAFE_BITSTREAM_READER` 1 (upstream's default: the bit reader stops at the packet end) |
| `shim/config_components.h` | generated `config_components.h` | `CONFIG_WMAV2_DECODER` 1, `CONFIG_WMAV1_DECODER` 0 |
| `shim/libavutil/avconfig.h` | generated `avconfig.h` | little endian, no fast-unaligned assumption |
| `shim/libavutil/thread.h` | `libavutil/thread.h` | `AVOnce` / `ff_thread_once` as a REAL once (Win32 `INIT_ONCE`, POSIX `pthread_once`). FFmpeg's no-threads fallback is a plain flag, and the game decodes on the loader thread and on the Stream worker at once; both can reach the lazily built sine-window and FFT tables |
| `shim/libavcodec/avcodec.h` | `avcodec.h` and the `codec.h`, `codec_id.h`, `packet.h`, `frame.h`, `samplefmt.h`, `channel_layout.h`, `avutil.h` it pulls in | only the fields `wmadec.c` / `wma.c` touch, under FFmpeg's names; private layouts |
| `shim/libavcodec/codec_internal.h` | `codec_internal.h` | `FFCodec` with the members `wmadec.c`'s initialiser names; `CODEC_LONG_NAME`, `FF_CODEC_DECODE_CB` |
| `shim/libavcodec/decode.h` | `decode.h` | declares `ff_get_buffer` (defined in the glue) |
| `shim/libavcodec/internal.h` | `libavcodec/internal.h` | `AVCodecInternal` only |
| `shim/aac_scalefactor_tables.c` | `libavcodec/aactab.c` | `ff_aac_scalefactor_code` / `_bits`, copied verbatim (upstream lines 181-209) under the file's licence header |
| `shim/wmadec_shim.c` | `libavutil/log.c`, `mem.c`, `cpu.c`, `mathematics.c`, `tx_double.c`, `tx_int32.c` | `av_log` silent; the `av_malloc` family over aligned malloc (64); `av_get_cpu_flags` 0; `ff_thread_once`; `av_gcd`, `av_realloc_f`, `av_fast_realloc` copied from the tag; empty double / int32 codelet lists (only float MDCTs are built) |
| `shim/wmadec_glue.c` | `avcodec_open2`, the decode loop, `avcodec_free_context` | implements `include/wmav2_decoder.h` over `ff_wmav2_decoder` |

## Build

One static library, built with fixed flags whatever the game's flavour (the way SDL3 is):
`-std=gnu11 -O2 -DNDEBUG -DHAVE_AV_CONFIG_H -w`, include path order
`include shim shim/libavcodec shim/libavutil ffmpeg`. The shim directories come first so a
quoted include of a replaced header (`"avcodec.h"`, `"thread.h"`) finds the shim; every
header that exists in `ffmpeg/` is found next to its includer first.

- Makefile: `$(WMADEC_LIB)` = `build/third_party/wmadec/libwmadec.a`, a plain file rule over
  its objects (the archive is its own stamp), linked by the exe and by `make unit`.
- CMake: `third_party/wmadec/CMakeLists.txt`, target `wmadec`, linked by `OpenBA` and `unit`.

`-w` silences the upstream sources only. Under the game's own `-Wall -Wextra` the extract
gives 79 warnings, every one in a verbatim upstream file (unused parameters in the transform
codelets, sign compares, six implicit fall-throughs and one pointer-sign in `wmadec.c`) and 0
in `shim/`. They are FFmpeg's and stay FFmpeg's.

## What XAudio2 2.7 does with an xWMA buffer (measured)

Measured on 2026-10-10 against XAudio2 2.7 (the engine retail uses) on three shipped sounds:
`Audio\Animal\Sheep1.wav` (mono 44.1 kHz), `Audio\Effects\Crowd.wav` (stereo 44.1 kHz) and
`Audio\Effects\Forest.wav` (stereo 48 kHz), all 48 kbit/s. XAudio2's decoded float was
captured through a pass-through XAPO on the source voice, with the mastering voice at the
source's rate and channel count (no resampling). FFmpeg's output equals XAudio2's to within
1.53e-5 (half an S16 step) on every sample once these rules are applied; the glue applies
rules 1 and 3, `src/engine/audio/xwma_decode.cpp` applies 2 and 4.

1. **xWMA has no codec header.** `wmav2_xwma_extradata` gives the 6 bytes FFmpeg's xWMA
   demuxer (`libavformat/xwma.c`) synthesises: encoder flags 0x1f at offset 4.
2. **Start of stream.** A decode starting at packet 0 drops the first 2 x frame_len frames
   (4096 at 44.1 and 48 kHz). A fresh decode starting at packet k > 0 drops 1 x frame_len.
3. **End of stream.** FFmpeg's EOF path (an empty packet) returns only the MDCT overlap and
   never decodes the frame still waiting in the bit reservoir. XAudio2 decodes it.
   `wmav2_drain` therefore first feeds the decoder a terminator superframe (one frame, bit
   offset 0, zero payload) through its normal packet path, which completes that frame from
   the bits it already holds, and then the empty packet. No upstream line is changed.
4. **Length.** From packet 0 the output is exactly the seek table's last entry / (2 x channels)
   frames. From packet k > 0 it is that minus the entry before k, minus frame_len.
5. **Chunks.** Submitting the packets as several buffers on one voice (the Stream worker's
   16-packet chunks, tables rebased per chunk) gives output bit-identical to one buffer.

Two shipped sounds end differently (checked after the rules above, 2026-10-10; the scan of
`WinData/data0.pkg` found 25 SoundBuffers stored in the clear, two of them with a last packet
whose table entry repeats the one before):

- **`Audio\Effects\Tornado.wav`** (stereo 48 kHz, 200 packets): FFmpeg rejects packets 194-199
  (`AVERROR_INVALIDDATA`); up to there it equals XAudio2 within 1.56e-5. XAudio2 decodes those
  six packets into about 5,000 frames of full-scale noise (peak 1.0) and then silence, and plays
  1,183,232 samples, not the table's 1,212,928. With this decoder the sound ends 5,632 samples
  earlier and without the burst; `XwmaDecoder::Failed()` is true for it. Retail plays that burst
  at the end of every tornado loop.
- **`Audio\Tool\Hoe2.wav`** (mono 44.1 kHz, 7 packets): the last 2048 frames differ from XAudio2
  by up to 3.8e-4 (about 12 S16 steps) in a tail whose peak is 0.0037; everything before is
  within 1.5e-5.

## Licence

FFmpeg's WMA decoder is **LGPL-2.1-or-later**: each verbatim file carries its notice, and
`COPYING.LGPLv2.1` is the licence text. The shim and glue files are offered under the same
licence. `OpenBA.exe` links this library STATICALLY, so whoever distributes the exe must
meet LGPL-2.1 section 6:

- give prominent notice with the exe that it uses this library and that the library is
  covered by the LGPL, and ship `COPYING.LGPLv2.1` (and this file) with it;
- make the library's source available (this directory: the verbatim upstream files, the
  shim, and the tag they came from);
- **the relink offer**: let the user rebuild the exe against a modified library. Section 6(a)
  is met by making the complete source of the exe available (this repository): modify or
  replace files under `third_party/wmadec/`, then `mingw32-make standalone` (or the CMake
  target `OpenBA`) rebuilds `libwmadec.a` and relinks the exe. A binary-only
  distribution would instead have to ship the exe's object files and a link line.

## Updating the pin

Fetch the new tag the same way into a scratch directory, copy the files in the table over,
diff them against the old ones, regenerate the sha256 list, rebuild, and run `make unit`:
`tests/unit/xwma_decode_test.cpp` holds the XAudio2 reference.
