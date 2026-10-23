# Changelog: v5 (2026-10-22)

**v5 changes one printed part, the base.** The lid and collar are geometrically identical to v4: re-exported from `enclosure_v5.scad`, they show zero boolean difference against `lid.stl` and `collar.stl`. So only these files are new:
- `base_v5.stl`
- `enclosure_v5.scad`
- `assembly_visual_v5.scad`
- `render_v5_*.png`

The v4 files are untouched: `base.stl`, `lid.stl`, `collar.stl`, `enclosure.scad`.

## Change 1: M2 pilot holes run 1.4 mm deeper (base only)

| | v4 | v5 |
|---|---|---|
| Pilot bottom, all 12 standoffs | z +0.8 (inside the post) | z -0.6 (into the 2 mm floor; 1.4 mm of floor left under each pilot) |
| Pilot diameter, post OD, post heights, positions | 1.7 / 5.0 / 3,3,5 mm / Eagle patterns | unchanged |

**Reason.** The M2 kit Josh is likely to buy (QOOSIKICC 615 pcs) has M2x4/6/8/10/12/16/20/25 and **no M2x5**. With v4 pilots, both usable lengths run 0.2 mm past the bottom of the pilot into solid plastic. That is where a thread-cutting screw jams and strips its 2.2 mm of thread.

**Evidence.** A mesh boolean of each screw shank against the base: `m2check.py` in the session tools, using the assembled frame and pilot bottoms measured from the STL.

| Post | Screw | v4 result | v5 result |
|---|---|---|---|
| Feather / DS2484, 3 mm | M2x4 | tip z +0.6, **0.59 mm³ per screw in solid below the pilot** | tip clear, 1.2 mm free below it; 2.4 mm engaged |
| ToF, 5 mm | M2x4 | clear; 2.4 mm engaged | clear; 2.4 mm engaged |
| ToF, 5 mm | M2x6 | **0.59 mm³ in solid** | tip clear; 4.4 mm engaged |
| any | M2x8 | into the floor / pokes through | still too long. Do not use. |

**Why it is safe.**
- Removed volume: 37.8 mm³ (12 x pi x 0.85² x 1.4 = 38.1 nominal).
- No material was added: v5 minus v4 = 0.0016 mm³, which is tessellation noise.
- The overhang and layer audit is identical to v4.
- Datums that did not move: board hole patterns, window axis, corner-screw stack, lid hole positions.

**Hardware with v5:**
- 12x M2x4 work everywhere.
- Optionally use M2x6 on the 4 ToF posts for more thread.
- The head must be **3.65 mm across or less** (see "Not changed" below).

## Verification of base_v5.stl, from scratch

| Check | Result |
|---|---|
| Watertight / one body | yes / 1 body (trimesh). OpenSCAD reports "Simple: yes". |
| Volume / extents | 25.98 cm³ / 113.80 x 102.14 x 19.00 mm |
| Window | 10.0 mm top, 13.95 mm at the underside, centred exactly on the ToF standoff centroid (24.700, 9.890) |
| Standoffs | 12, OD 5.0, pilot 1.7, tops at 3.0 / 3.0 / 5.0 mm, positions unchanged; pilot bottom -0.6 on all 12 |
| Screw stack vs. v4 collar and lid | axis offset 0.000 mm on all 4 corners. Holes: 3.4 (collar), 3.4 + 6.2 recess (base), 2.6 (lid). |
| Overhang audit (faces > 45°, print orientation) | same as v4: 4 recess ceilings, zip-tie tunnel ceiling, USB cutout top. The pilot bottoms face up, so they are not overhangs. |
| Layer audit (0.2 mm layers, 45° support allowance) | 95 layers. Bridges: zip-tie tunnel (2.6 mm) and USB cutout top (8.5 mm long across the 2.2 mm wall). One-layer annular overhangs (1.4 mm wide) at the 4 screw-recess ceilings. Nothing else is unsupported. |
| Slicer | **None installed** (no Bambu Studio, OrcaSlicer or PrusaSlicer; winget lists none). Not downloaded, per instructions. The layer audit above stands in for it. |
| Renders | `render_v5_assembled.png`, `render_v5_exploded.png`, `render_v5_top.png` |

## Not changed, and why (findings that need a measurement first)

**USB-C cutout (12.5 x 7.5 mm, corner radius 2)**
- A square-cornered plug housing fits only up to 10.43 mm wide at 7.0 mm tall, or 11.14 mm wide at 6.5 mm tall.
- A housing with 1.5 mm corners fits up to 12.5 x 6.5.
- The receptacle face is 2.66 mm behind the outside of the wall. A housing the cutout blocks therefore mates 1.2-2.7 mm short, which is not reliable.
- Josh's cable is unmeasured, so the cutout stays as is. Measure the overmold width, height and corner radius.

**M2 head vs. ESP32-S3 module**
- At Feather hole (48.26, 20.955) the module edge is 1.825 mm from the hole centre (module body 15.4 mm, assumed from memory).
- A head over 3.65 mm across touches the module. A button head (3.5 mm) clears it by about 0.1 mm. A DIN 912 socket head (3.8 mm) touches.
- This is hardware, not a print change: use 3.5 mm heads or nylon M2 screws, or leave those two screws out.

**Feather STEMMA QT plug vs. C2/U2**
- The model has the plug body and cable 0.85 mm into C2/U2 behind the connector. That is the only non-designed hard contact in the assembly.
- It comes from the assumed opening direction and plug height. It is not an enclosure issue. A photo of the Feather's QT port settles it.

## Documentation correction found during the check

- The v4 AUDIT and cable routing used Feather origin y = 32.64. The actual origin is **31.78** (= `IY - back_gap - f_wid`). The print files were always correct; only the hand-computed coordinates in the docs and routes were 0.86 mm off.
- Corrected: `route_cables.py`, `make_assembly_stls.py` and AUDIT row 53 (boot guide tube vs. QT connector: 0.39 mm plan gap, 2.1 mm vertical).
- Cable routes were also rebuilt with real bend radii: smallest 3.4 mm (QT) and 3.8 mm (probe drape). The v4 splines had kinks under 1 mm.

## Print order (unchanged)

1. Collar first. Test-fit it on all four vessels: Karter 250 mL, Pyrex 400 mL, both jars.
2. Then the base (v5) and lid.
