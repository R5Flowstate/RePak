# RePak ( R5Flowstate/S21 )

Builds Respawn RPak archives from JSON asset manifests. This fork writes
**Season 21 native asset versions**, plus a **dedi** path that routes models
and animations back to their S3 versions.

Agents view included: CLAUDE.md

## S21 writers

| 4cc | ver |
|-----|-----|
| mdl_ | 17 |
| aseq | 11 |
| arig | 6 |
| matl | 23 |
| shdr | 15 |
| shds | 12 |
| txtr | 10 |
| txtx | 2 |
| uiia | 2 |
| rmap | 4 |
| wrap | 7 |
| efct | 16 |
| rui | - |

`"dedi": true` on a build list emits mdl_ **v10**, aseq **v7** and arig **v4**
with zstd compression, skipping VG and materials.

## Usage

```
repak <buildMap.json | buildList.json>
repak -pakguid <string>
repak -uimghash <string>
repak -compress <pak> [level] [workers]
repak -decompress <pak>
```

```
# build one pak from its manifest
repak common.json

# build several, S3 dedicated-server versions -- {"paks":[...],"dedi":true}
repak build_list.json

# compute the GUID a name hashes to
repak -pakguid material/models/props/skull_01_rgdp.rpak
```

Asset order inside a manifest: models first, then
dtbl / shdr / shds / txan / txtr / txtx / matl / aseq / arig / rmap / uiia.

Oodle is **not** bundled - it is Epic Games Tools middleware. Supply the SDK
yourself in `src/thirdparty/oodle/`; see the note there.

Upstream: [r-ex/RePak](https://github.com/r-ex/RePak), by way of kral's
R5Valkyrie fork.
