# Particle uber converter

Repak refuses to pack a PTCU / PTCS material whose `.uber` sidecar is 240
bytes:

```
Particle uber "..." is 240 bytes (S30). S21 PTCU/PTCS is 144. Convert before packing.
```

That sidecar came out of a newer season's pak. S21 particle materials carry a
144-byte uber buffer, and the extra bytes are not the only difference: three
slots inside the shared prefix changed meaning, so a plain truncation ships a
value the S21 shader reads as a float and the client crashes on the render
thread.

```
py -3 convert_particle_uber.py <exported material dir> -o <out dir>
py -3 convert_particle_uber.py material/foo_ptcu.uber --in-place
```

Rules applied (measured on 831 materials shipped by both seasons):

| offset | newer season | S21 | rule |
|---|---|---|---|
| `+0x2C` | `0xFFFFFFFF` | `0.5` | always write `0.5` |
| `+0x20` | `inf` marks unused | `0` | write `0` only when the source is `inf` |
| `+0x5C` | packed integers | `0` | write `0` |

Everything else in the first 144 bytes is copied as is, including the other
`inf` values S21 also stores.

The uber is one of three things that change for a newer-season particle
material. The other two are not handled here:

* `shaderSet` must point at an S21 shader set (v12). A newer season's shds
  will not load. Bind the shader set of a stock S21 material of the same
  `shaderType` and with the same texture count.
* `blendStateMask` should be the value that donor material carries.
