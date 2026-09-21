# RePak (R5Flowstate / S21)

Builds Respawn RPak archives from JSON asset maps. This fork writes **Season 21
native asset versions** for the R5Flowstate S21 client, plus a **dedi** path
that routes models and animations back to their S3 versions. Upstream RePak and
kral's R5Valkyrie fork targeted Titanfall 2, R5Reloaded (S3) and R5Valkyrie.

Upstream: [r-ex/RePak](https://github.com/r-ex/RePak), by way of kral's
R5Valkyrie fork. Agents view included: CLAUDE.md.

## What this fork adds

S21-native writers (what the S21 client actually loads):

| 4cc | ver | notes |
|-----|-----|-------|
| mdl_ | 17 | field-relative LOD groups; stale compressed VG offsets rewritten to match the raw streamed `.vg` |
| aseq | 11 | |
| arig | 6 | |
| matl | 23 | |
| shdr | 15 | |
| shds | 12 | |
| txtr | 10 | synthesizes `$hdrTail` when streamed mips omit it |
| txtx | 2 | |
| uiia | 2 | |
| rmap | 4 | |
| wrap | 7 | |
| efct | 16 | 24-byte header + baked ParticleDefinition blob + pointer replay |
| rui | - | |

Also:

- `"dedi": true` on a build list emits mdl_ **v10**, aseq **v7** and arig **v4**
  with zstd compression, skipping VG and materials.
- Header pages are append-only, so pak-load compaction cannot leave unpatched
  descriptors.
- Per-entry `$assetsDir` lets one pak merge entries from several asset trees.
- `tools/particle_uber/` converts newer-season particle material uber buffers
  to the S21 layout.

Asset order inside a manifest: models first, then
dtbl / shdr / shds / txan / txtr / txtx / matl / aseq / arig / rmap / uiia.

## Table of Contents

1. [Introduction](#introduction)
2. [Project Overview](#project-overview)
3. [Installation](#installation)
4. [Build Configuration](#build-configuration)
5. [Asset Types Reference](#asset-types-reference)
6. [Asset JSON Schemas](#asset-json-schemas)
7. [Streaming System](#streaming-system)
8. [Advanced Features](#advanced-features)
9. [Examples](#examples)
10. [Troubleshooting](#troubleshooting)

---

## Introduction

**RePak** converts JSON asset definitions into binary RPak files. This
R5Flowstate fork is the S21 packer for the R5Flowstate map and content pipeline.

### Key Capabilities

- Multi-format asset compilation (17+ asset types)
- Zstandard compression with configurable levels
- Advanced streaming system with starpak support
- GUID-based asset referencing and dependency tracking
- Cross-platform pak file generation

---

## Project Overview

### What is RePak?

RePak is a command-line tool that converts JSON-based asset definitions into binary RPak files used by Resource games. It handles:

---

## Installation

### Prerequisites

- **Visual Studio 2022** or newer with the C++ desktop workload
- Zstandard, rapidjson and rapidcsv are vendored under `src/thirdparty/`
- **Oodle** is not bundled; it is Epic Games Tools middleware. Supply the SDK
  yourself in `src/thirdparty/oodle/`; see the note there.

### Building

```
git clone https://github.com/R5Flowstate/RePak.git
cd RePak
msbuild RePak.sln -p:Configuration=Release -p:Platform=x64
```

The compiled binary will be located at `bin/Release/repak.exe`

## repak Usage

```
For building pak files:
  repak <buildMapPath>
    <buildMapPath>  - path to a map file containing the build parameters for the pak to build

For creating stream caches:
  repak <streamingPath>
    <streamingPath> - path to a directory containing streaming files to be cached

For calculating Pak Asset GUIDs:
  repak -pakguid <strToGuid>
    <strToGuid>     - the string to compute the asset guid from

For calculating UI Image hashes:
  repak -uimghash <strToHash>
    <strToHash>     - the string to compute the uimg hash from

For compressing standalone paks:
  repak -compress <pakFilePath> [compressLevel] [workerCount]
    <pakFilePath>   - the target pak file to compress
    <compressLevel> - (optional) the level of compression [-5, 22]; default = 6
    <workerCount>   - (optional) the number of compression workers [1, 256]; default = 16

For decompressing standalone paks:
  repak -decompress <pakFilePath>
    <pakFilePath>   - the target pak file to decompress
```

---

## Build Configuration

The build process is driven by JSON configuration files. Here's the structure:

### Basic Configuration Template

```json
{
  "version": 8,
  "keepDevOnly": true,
  "name": "common",
  "assetsDir": "sdk_depot/others/common/",
  "outputDir": "./build/",
  "compressLevel": 6,
  "compressWorkers": 16,
  "files": [
    {
      "_type": "stlt",
      "_path": "settings_layout/settings_itemtype_quest_layout.rpak"
    }
  ]
}
```

### Configuration Fields

| Field | Type | Description |
|-------|------|-------------|
| `version` | `int` | Pak file version (7 Titanfall 2 / Northstar, 8 Apex / S21) |
| `name` | `string` | Output pak file name |
| `assetsDir` | `string` | Base directory for asset files |
| `outputDir` | `string` | Output directory for generated files |
| `compressLevel` | `int` | Zstandard compression level (0-22, default: 6) |
| `compressWorkers` | `int` | Number of compression threads |
| `keepDevOnly` | `bool` | Include asset names |
| `showDebugInfo` | `bool` | Enable verbose logging |
| `files` | `array` | List of assets to include |

### Build List Support

Repak can build multiple Respawn paks from single json file using this format:

```json
{
  "version": 8,
  "paks": [
    "common.json",
    "mp_maps.json",
    "streaming.json"
  ]
}
```

---

## Asset Types Reference

RePak supports **17+ asset types**, each with specific JSON schemas. The
versions below are the upstream ones; for the S21 versions this fork writes see
[What this fork adds](#what-this-fork-adds).

| Type ID | Name | Description | Version Support |
|---------|------|-------------|-----------------|
| `matl` | Material | Shader and texture definitions | v12-v15 |
| `txtr` | Texture | DDS format with mip levels | v8 |
| `txan` | Texture Animation | Animated texture sequences | v1 |
| `txls` | Texture List | Texture collections used for camo cosmetics| v1 |
| `rmdl` | Model | 3D mesh data | v10 |
| `aseq` | Animation Sequence | Respawn sequence animation format | v7 |
| `arig` | Animation Rig | Skeleton definitions | v4 |
| `shdr` | Shader | Compiled shader bytecode | v8, v12 |
| `shds` | Shader Set | Shader combinations | v8, v11 |
| `dtbl` | DataTable | Game datatables | v1 |
| `stlt` | Settings Layout | STGS asset layout definitions | v0 |
| `stgs` | Settings | Flavor settings/configurations | v1 |
| `mt4a` | Material for Aspect | Aspect-ratio materials | v3 |
| `uimg` | UI Image Atlas | UI texture atlases | v1 |
| `ptch` | Patch | Pak patch system | v1 |
| `rlcd` | LCD Effect | Screen effects | v1 |
| `anir` | Animation Recording | Pre recorded animations | v1 |

---

## Asset JSON Schemas

### Material (`matl`)

Defines visual materials with shaders and textures.

**Shader Types:** `rgdu`, `rgdp`, `rgdc`, `rgbs`, `sknu`, `sknp`, `sknc`, `wldu`, `wldc`, `ptcu`, `ptcs`

```json
{
  "name": "models/props/skull/skull_01",
  "width": 1024,
  "height": 1024,
  "depth": 0,
  "glueFlags": "0x56000020",
  "glueFlags2": "0x100000",
  "blendStates": [
    "0xF0000000", "0xF0000000", "0xF0000000", "0xF0000000",
    "0xF0000000", "0xF0000000", "0xF0000000", "0xF0000000"
  ],
  "blendStateMask": "0x4",
  "depthStencilFlags": "0x17",
  "rasterizerFlags": "0x6",
  "uberBufferFlags": "0x0",
  "features": "0x1F5A92BD",
  "samplers": "0x1D0300",
  "surfaceProp": "concrete",
  "surfaceProp2": "",
  "shaderType": "rgdp",
  "shaderSet": "0x75AB3C79B6CB8EE8",
  "$textures": {
    "0": "0xC634AB9A1FECD19",
    "1": "0x4F07A71295A1027"
  },
  "$depthShadowMaterial": "0x251FBE09EFFE8AB1",
  "$depthPrepassMaterial": "0xE2D52641AFC77395",
  "$depthVSMMaterial": "0xBDBF90B97E7D9280",
  "$depthShadowTightMaterial": "0x85654E05CF9B40E7",
  "$colpassMaterial": "0x581CFA08311A82AB"
}
```

### Texture (`txtr`)

Image assets with streaming support.

```json
{
	"streamLayout": [
		"permanent",
		"permanent",
		"permanent",
		"permanent",
		"permanent",
		"permanent",
		"permanent",
		"optional"
	],
	"mipInfo": [
		11,
		10,
		13,
		23,
		45,
		119,
		193,
		255
	],
	"resourceFlags": "0x0",
	"usageFlags": "0x0"
}

```

### Settings (`stgs`)

Game configuration and item definitions.

```json
{
  "layoutAsset": "settings_layout/settings_itemtype_ability_layout.rpak",
  "uniqueId": 484701158,
  "settings": {
    "assetName": "settings/itemflav/ability/dummie_tactical.rpak",
    "itemType": "ability",
    "devDescription": "PLEASE DESCRIBE",
    "localizationKey_NAME": "#ABL_ITEM_SPAWNER",
    "quality": "NONE",
    "icon": "rui/hud/tactical_icons/tactical_dummie_mode",
    "weaponClassname": "mp_ability_item_spawner",
    "tags": []
  }
}
```

### DataTable (`dtbl`)

Game datatables. Basic CSV format

```json
| levelIndex | xpPerLevel | reward                                                    | rewardQty | premium | notes |
| ---------- | ---------- | --------------------------------------------------------- | --------- | ------- | ----- |
| 0          | 29500      | settings/itemflav/weapon_skin/dmr/s03bp_legendary_01.rpak | 1         | true    |       |
| 0          | 0          |                                                           | 1         | true    |       |
| 0          | 0          |                                                           | 1         | true    |       |
| 0          | 0          |                                                           | 1         | true    |       |
| 0          | 0          | settings/itemflav/xp_boost/battlepass_season03.rpak       | 1         | true    |       |
| 1          | 29500      | settings/itemflav/weapon_charm/fireball.rpak              | 1         | true    |       |
| 1          | 0          | settings/itemflav/loadscreen/s03bp_11.rpak                | 1         | false   |       |
| ---------- | ---------- | --------------------------------------------------------- | --------- | ------- | ----- |
| int        | int        | asset  or asset_noprecache                                | int       | bool    |string |

```
A little note on datatable .csv files, using asset will make that asset a dependency, which means it needs to be loaded before the datatable to avoid getting dependency error in game.

---

## Streaming System

RePak implements an advanced streaming system for large assets:

### Streaming Tiers

| Tier | Description | Use Case |
|------|-------------|----------|
| **Permanent** | Always loaded | Core game assets |
| **Mandatory** | Streamed mandatory data, has to be loaded  | Higher level texture mipmap levels and model data |
| **Optional** | Streamed optional data | Highest level texture mipmap levels, not mandatory |


### Configuration

```json
    "streamFileMandatory": "paks/Win64/pc_all(01).starpak",
    "streamFileOptional": "paks/Win64/pc_all(01).opt.starpak"
```

---

## Advanced Features

### GUID Referencing

Assets reference each other using GUIDs:

```json
    {
      "_type": "dtbl",
      "_path": "datatable/unknown_name.rpak",
      "$guid": "0x23363D64F267F77"
    }
```

### Patch System

The `ptch` asset type allows game to load newer patched version of the same pak:

```json
    {
      "_type": "Ptch",
      "_path": "patch_master",
      "entries": [
        {
          "name": "loadscreen_custom_01.rpak",
          "version": 2 // number of  patches
        },
        {
          "name": "loadscreen_custom_02.rpak",
          "version": 2
        },
      ]
    }
```

### Dependency Tracking

RePak automatically tracks dependencies:
- Internal dependencies (assets within the pak)
- External references (assets from other paks)
- Circular dependency detection

---

## Examples

### Complete Material Setup

```json
// common.json entry
{
  "_type": "matl",
  "_path": "material/models/props/skull_01_rgdp.rpak"
}
```

```json
// material/models/props/skull_01_rgdp.json
{
	"name": "models/props/skull/skull_01",
	"width": 1024,
	"height": 1024,
	"depth": 0,
	"glueFlags": "0x56000020",
	"glueFlags2": "0x100000",
	"blendStates": [
		"0xF0000000",
		"0xF0000000",
		"0xF0000000",
		"0xF0000000",
		"0xF0000000",
		"0xF0000000",
		"0xF0000000",
		"0xF0000000"
	],
	"blendStateMask": "0x4",
	"depthStencilFlags": "0x17",
	"rasterizerFlags": "0x6",
	"uberBufferFlags": "0x0",
	"features": "0x1F5A92BD",
	"samplers": "0x1D0300",
	"surfaceProp": "concrete",
	"surfaceProp2": "",
	"shaderType": "rgdp",
	"shaderSet": "0x75AB3C79B6CB8EE8",
	"$textures": {
		"0": "0xC634AB9A1FECD19",
		"1": "0x4F07A71295A1027",
		"2": "0x470B3C2CD2B17E73",
		"3": "0x73BE1738040314D2",
		"5": "0x72D1217DF3137908"
	},
	"$textureTypes": {
		"0": "albedoTexture",
		"1": "normalTexture",
		"2": "glossTexture",
		"3": "specTexture",
		"5": "aoTexture"
	},
	"$depthShadowMaterial": "0x251FBE09EFFE8AB1",
	"$depthPrepassMaterial": "0xE2D52641AFC77395",
	"$depthVSMMaterial": "0xBDBF90B97E7D9280",
	"$depthShadowTightMaterial": "0x85654E05CF9B40E7",
	"$colpassMaterial": "0x581CFA08311A82AB",
	"$textureAnimation": "0x0"
}

```


## Inherited from R5Valkyrie

### Supported Shader Types

This version includes additional material types:
- **`rgbs`** - Mostly used by Catalyst abilities

### Additional Features

- Missing dependency checks
- Progress bar
- Unix time stamp for changing pak build date (affects load order in game)
- Better uimg handling

---

Maintained by [R5Flowstate](https://github.com/R5Flowstate).
Based on kral's R5Valkyrie fork of [r-ex/RePak](https://github.com/r-ex/RePak).
