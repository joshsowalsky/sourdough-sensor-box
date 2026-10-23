// Sourdough sample monitor enclosure, v5 (base + lid + universal collar for 4 vessels)
// v5 = v4 (enclosure.scad, unchanged) + ONE change: the 12 M2 pilot holes run 1.4 mm deeper, into the
// floor (bottom at z = pilot_bottom_z = -0.6 instead of +0.8), so M2x4 on the 3 mm posts and M2x6 on the
// 5 mm ToF posts never bottom out. Reason and evidence: CHANGELOG-v5.md. Lid and collar are identical to v4.
// Boards: Adafruit ESP32-S3 Feather #5323, VL53L4CD ToF #5396, DS2484 1-Wire #5976
//
// Tags used in comments:
//   [SRC:<file>]  value read from Adafruit's Eagle .brd file (fetched 2026-10-10 from GitHub)
//       feather.brd = adafruit/Adafruit-Feather-ESP32-S3-PCB  "Adafruit ESP32-S3 8MB No PSRAM.brd"
//       tof.brd     = adafruit/Adafruit-VL53L4CD-PCB          "Adafruit VL53L4CD.brd"
//       ds.brd      = adafruit/Adafruit-DS2484-I2C-to-1-Wire-Bus-Adapter-Breakout-PCB
//                     "Adafruit DS2484 I2C to 1-Wire Breakout.brd"
//   [SRC:order]   value from the Adafruit order / product listing
//   [SRC:equip]   value from the Equipment planning session (beaker, probe prep)
//   [ASSUMED]     design choice or unverified estimate -> measure before printing (see AUDIT.md)
//
// Coordinates: interior of base, X = length, Y = depth (front wall at y=0), Z up,
// floor top surface at z=0 (base underside at z=-floor_t). Units: mm.
//
// part = "base" | "lid" | "collar" | "assembly" (preview, ghost boards + beaker) | "print"

part = "assembly";

$fn = 64;
eps = 0.01;

// ---------------- Board data ----------------
// Feather ESP32-S3 (#5323)
f_len   = 50.8;     // [SRC:feather.brd] outline 0..50.8
f_wid   = 22.86;    // [SRC:feather.brd] outline 0..22.86
pcb_t   = 1.6;      // [ASSUMED] standard FR4 thickness, not stated in .brd
// Holes (board coords). 2.54 ones: MOUNTINGHOLE_2.5_PLATED, drill 2.5 / pad 3.2; 48.26 ones: 2.2 drill.
f_holes = [[2.54, 2.54], [2.54, 20.32], [48.26, 1.8415], [48.26, 20.955]]; // [SRC:feather.brd]
f_usb_y   = 11.43;  // [SRC:feather.brd] USB-C (X3) centre y (at x=2.667, rot R270)
f_usb_overhang = 1.14; // [SRC:feather.brd] USB-C doc outline reaches x=-1.14 (past board edge)
f_usb_w   = 8.94;   // [SRC:feather.brd] USB-C doc outline width (+-4.47)
f_usb_h   = 3.2;    // [ASSUMED] CUSB31-CFM2AX receptacle height above PCB top (not in .brd)
f_qt_pos  = [22.352, 11.43]; // [SRC:feather.brd] STEMMA QT (CONN1), rot R270 -> opening faces -X
f_parts_h = 3.5;    // [ASSUMED] tallest top-side part (ESP32-S3-MINI-1 module) for ghost model
// NOTE: assumes NO headers soldered to the Feather.

// VL53L4CD ToF (#5396)
t_len   = 25.4;     // [SRC:tof.brd] outline 0..25.4  (order listing says 25.5)
t_wid   = 17.78;    // [SRC:tof.brd] outline 0..17.78 (order listing says 17.7)
t_holes = [[2.54, 2.54], [22.86, 2.54], [2.54, 15.24], [22.86, 15.24]]; // [SRC:tof.brd] drill 2.5
t_sensor = [12.7, 8.89]; // [SRC:tof.brd] U1 (LGA12) centre = board centre, TOP side
t_qt_x  = [2.667, 22.733]; // [SRC:tof.brd] CONN4 (R270) / CONN3 (R90), y=8.89, open toward short edges
t_parts_h = 3.0;    // [ASSUMED] JST SH connector body ~2.9 mm (SM04B-SRSS-TB) -- measure
t_sensor_h = 1.0;   // [ASSUMED] VL53L4CD package height (ST: 4.4 x 2.4 x 1.0, not re-checked)

// DS2484 1-Wire (#5976)
d_len   = 25.4;     // [SRC:ds.brd]
d_wid   = 17.78;    // [SRC:ds.brd]
d_holes = [[2.54, 2.54], [22.86, 2.54], [2.54, 15.24], [22.86, 15.24]]; // [SRC:ds.brd] drill 2.5
d_term  = [12.7, 14.351]; // [SRC:ds.brd] JP2 3-pin 2.54 mm terminal centre, pins along X
d_term_ext = [8.12, 6.3]; // [SRC:ds.brd] terminal silkscreen extent (x +-4.06, y -3.1..+3.2)
d_h     = 10.0;     // [SRC:order] overall height 10.0 incl. terminal block
// Wire entry ASSUMED to face the board edge nearest the block (y=17.78 edge).

// ---------------- Enclosure parameters ----------------
wall    = 2.2;      // [ASSUMED]
floor_t = 2.0;      // [ASSUMED]
ih      = 17.0;     // [ASSUMED] interior height (DS2484 top at 3+10=13 mm)
corner_r = 3.0;     // [ASSUMED]

f_so_h  = 3.0;      // [ASSUMED] Feather standoff height
t_so_h  = 5.0;      // [ASSUMED] ToF standoff (board upside down; JST bodies ~2.9 hang below)
d_so_h  = 3.0;      // [ASSUMED]
so_d    = 5.0;      // [ASSUMED] standoff OD
so_pilot = 1.7;     // [ASSUMED] pilot for M2 self-tapping screws (all board holes >= 2.2)

f_usb_gap = 1.6;    // [ASSUMED] Feather edge to inner wall (USB-C overhangs 1.14)
tof_x   = 12.0;     // [ASSUMED] left clearance: JST SH plug (~4.3 deep) + cable U-turn
tof_front = 1.0;    // [ASSUMED] ToF board edge to front wall. v2: ToF moved to the front so the
                    //   probe notch in the collar is outside the box footprint (see collar).
front_gap = 10.0;   // [ASSUMED] front wall to DS2484 terminal edge, room for probe wires
row_gap = 4.0;      // [ASSUMED]
board_gap = 16.0;   // [ASSUMED] ToF <-> DS2484: two facing JST plugs + cable loop
right_gap = 3.0;    // [ASSUMED]
back_gap = 1.5;     // [ASSUMED]

IX = tof_x + t_len + board_gap + d_len + right_gap;
IY = front_gap + d_wid + row_gap + f_wid + back_gap;

f_org = [f_usb_gap, IY - back_gap - f_wid];
t_org = [tof_x, tof_front];
d_org = [tof_x + t_len + board_gap, front_gap];
// ToF is mounted FLIPPED about the X axis (sensor faces down): board y -> t_wid - y.
// DS2484 is ROTATED 180 deg about Z so its terminal faces the front wall.
// Feather is neither rotated nor flipped (USB end at x=0).
function d_xy(p) = [d_org[0] + d_len - p[0], d_org[1] + d_wid - p[1]];
function t_xy(p) = [t_org[0] + p[0], t_org[1] + (t_wid - p[1])];
function f_xy(p) = [f_org[0] + p[0], f_org[1] + p[1]];

tof_c = t_xy(t_sensor);      // window / beaker axis (x,y)

// ToF window through base floor
win_d   = 10.0;     // [ASSUMED] open window; FoV 18 deg (ST, not re-checked)
win_chamfer = 2.0;  // [ASSUMED] 45 deg flare toward the underside

// USB-C cutout (left wall, x=0)
usb_cut_w = 12.5;   // [ASSUMED] fits a plug overmold up to ~12 x 7 mm
usb_cut_h = 7.5;    // [ASSUMED]
usb_c  = [f_org[1] + f_usb_y, f_so_h + pcb_t + f_usb_h/2];

// Probe cable exit (front wall) - U slot, lid tongue clamps the cable
probe_cable_d = 4.0;  // [SRC:equip] DS18B20 PVC cable OD 4 mm
probe_d   = 5.0;      // [ASSUMED] slot width (cable + 1 mm)
probe_x   = d_xy(d_term)[0];
probe_z   = 7.0;      // [ASSUMED] centre of slot bottom radius above floor top
clamp_crush = 0.4;    // [ASSUMED] tongue presses the cable this much
zt_w = 3.0; zt_h = 1.6;  // [ASSUMED] zip-tie tunnel for 2.5-3 mm ties

// Lid / fastening
lid_t   = 3.0;      // [ASSUMED]
lip_h   = 2.0;      // [ASSUMED]
lip_t   = 1.2;      // [ASSUMED]
fit     = 0.25;     // [ASSUMED] clearance per side
lug_d   = 8.0;      // [ASSUMED]
lug_off = 1.5;      // [ASSUMED]
lid_lug_drop = 8.0; // [ASSUMED] v2: 8 (was 5) so M3x20 still bites 6.5 mm through the collar
screw_clear = 3.4;  // [ASSUMED] M3 clearance
screw_head_d = 6.2; // [ASSUMED]
screw_head_h = 3.0; // [ASSUMED]
screw_pilot = 2.6;  // [ASSUMED] M3 self-tap in PETG (4.0 for heat-set inserts)

// ---------------- Vessels + universal collar (v4) ----------------
// One FLAT plate rests directly on the rim of any of Josh's four vessels:
//   Karter 250 mL tall beaker, Pyrex 400 mL tall (Berzelius) beaker, 2x YILIHCIA 34 oz jar.
// Its underside is flat except two sets of 3 short centring tabs:
//   - "small" tabs fit inside the 50 mm beaker bore (the narrowest). In the 400 mL beaker
//     they hang free and limit sideways slop to ~8.5 mm. In the jar they hang free.
//   - "jar" tabs fit inside the jar mouth. They sit outside both beakers' rims.
// The window is always on the vessel axis, so it is always over the opening. The probe notch
// runs radially from r=13 to the plate edge, so the probe can drop in at r=13..21 (inside the
// 50 mm bore) or anywhere further out on wider vessels. That resolves the window-vs-notch
// conflict: both sit within the narrowest bore, and the notch stays open outward for the rest.
// No feet: the sensor height above the rim is fixed by the plate (see echo). Depth is read
// against an empty-vessel baseline, so no height adjustment is needed.
// Vessel data: [name, bore, rim_od, height, base_r, empty_g]  -- ALL [ASSUMED] (no measurements)
//   250: bore 50 [SRC:equip "about 50"]; wall 2.5 -> rim OD 55; tall form ~120 high
//   400: OD 70 x 130 [SRC: DWK/Fisher listings for the same tall-form design, via coordinator];
//        bore = 70 - 2x3 = 64 (wall ~3 mm per side, inferred, not measured)
//   jar: mouth 99..105 (similar 34 oz jars list 3.9-4.13 in); rim lip ~4 mm -> rim OD 113
vessels = [["Karter 250 mL tall", 50, 55, 120, 27.5, 25],
           ["Pyrex 400 mL tall", 64, 70, 130, 35, 110],
           ["YILIHCIA 34 oz jar", 105, 113, 160, 50, 350]];
vessel  = 0;        // preview only: which vessel the assembly is shown on
jar_mouth_d   = 105;  // [ASSUMED] design mouth; THE one jar parameter (plate size follows it)
jar_mouth_tol = 6;    // [ASSUMED] jar tabs also fit a mouth this much smaller (99 mm)
col_t     = 3.0;    // [ASSUMED] plate thickness (M3 countersunk head 1.7 + 1.3 under it)
plate_r   = jar_mouth_d/2 + 9;   // [ASSUMED] covers the jar rim even with 4 mm slop
tab_len   = 4.0;    // [ASSUMED] tab depth below the plate
tab_t     = 2.0;    // [ASSUMED]
tab_arc   = 30;     // [ASSUMED] degrees
small_bore = vessels[0][1];
small_tab_ro = small_bore/2 - 1.5;               // [ASSUMED] 1.5 mm clearance (bore +-1)
small_tab_ang = [0, 120, 240];
jar_tab_ro = (jar_mouth_d - jar_mouth_tol)/2 - 1.0; // 48.5: fits 99..105 mouths
jar_tab_ang = [90, 210, 330];
cs_d = 6.3; cs_h = 1.8;  // [ASSUMED] countersink for M3 flat head (ISO 10642: dk 6.0, k 1.7)
col_win_d = 12.0;   // [ASSUMED] collar window (cone from base window)
bk_bore = vessels[vessel][1]; bk_rim_od = vessels[vessel][2]; bk_h = vessels[vessel][3];
// Probe notch: straight down into the beaker, in front of the box (-Y from the window centre)
probe_tip_d = 6.0;        // [SRC:equip] stainless tip OD
shrink_wall = 0.6;        // [ASSUMED] recovered wall of 3/8"->3/16" food-grade heat shrink
probe_tip_od = probe_tip_d + 2*shrink_wall;   // ~7.2 heat-shrunk tip
notch_w   = 10.0;   // [ASSUMED] >= probe_tip_od + 2.8 mm (can tilt the tip in)
notch_r0  = 13.0;   // [ASSUMED] notch inner end, radius from axis; must be > front edge distance

// ---------------- v3: counterweight pod (stability) ----------------
// The box's centre of mass sits ~24 mm off the beaker axis toward +X/+Y. A coin pocket on an
// arm off the front-left corner (opposite side) pulls it back inside the feet triangle.
// Sized for US quarters: 24.26 mm dia, 1.75 mm thick, 5.67 g  [SRC: US Mint spec, from memory]
cw_on      = true;
cw_c       = [-10.0, -24.0];  // [ASSUMED] pod centre (interior coords); chosen clear of the
                              //   front-left lug screw, the probe notch and the beaker (r >= 35)
cw_pocket_d = 25.0;  // [ASSUMED] quarter + 0.74 clearance; 3 crush ribs grip the stack
cw_rib     = 0.45;   // [ASSUMED] rib protrusion (0.1 mm interference on a 24.26 coin)
cw_wall    = 1.8;    // [ASSUMED]
cw_floor   = 1.2;    // [ASSUMED]
cw_h       = floor_t + ih - lid_lug_drop;  // pod height = base lug height (stays under the lid lug)
cw_coins   = floor((cw_h - cw_floor) / 1.75);   // quarters that fit (stack below the rim)

// ---------------- v3: lid access holes (reset / boot / NeoPixel) ----------------
f_reset = [10.795, 5.1435];  // [SRC:feather.brd] SW2 "Reset", BTN_KMR2_4.6X2.8, top side
f_boot  = [20.955, 5.207];   // [SRC:feather.brd] SW1 "Boot",  BTN_KMR2_4.6X2.8, top side
f_neo   = [29.4005, 8.128];  // [SRC:feather.brd] LED1 SK6805-1515 NeoPixel, top side
btn_h   = 2.1;      // [ASSUMED] KMR2 actuator height above PCB (not in .brd)
poke_d  = 2.6;      // [ASSUMED] fits a round toothpick (~2.0-2.3) or a 2 mm screwdriver
guide_od = 5.0;     // [ASSUMED] guide tube under the lid (also blocks splashes sideways)
guide_bot = 9.5;    // [ASSUMED] tube bottom z: above the STEMMA plug (~7.6) and ESP module (~7.8)
led_mode = "hole";  // "hole" = 1.5 mm through-hole; "skin" = 0.4 mm closed skin (only glows
                    //   through with natural/translucent PETG)
led_hole_d = 1.5;   // [ASSUMED]
led_pipe_id = 3.0;  // [ASSUMED] light chimney inner dia
led_skin = 0.4;     // [ASSUMED] 2 layers at 0.2

OX = IX + 2*wall; OY = IY + 2*wall;
base_h = floor_t + ih;
lugs = [[-wall - lug_off, -wall - lug_off], [IX + wall + lug_off, -wall - lug_off],
        [-wall - lug_off, IY + wall + lug_off], [IX + wall + lug_off, IY + wall + lug_off]];

// sensor face height above the rim (screws fully in .. fully out)
sens_above_base_bottom = floor_t + t_so_h - t_sensor_h;
sens_min = sens_above_base_bottom + col_t;
front_edge_dist = tof_c[1] + wall;   // window axis -> outer front face of the box
echo(str("Interior ", IX, " x ", IY, " x ", ih, " ; box ", OX, " x ", OY, " x ", base_h + lid_t));
echo(str("ToF window axis at interior (", tof_c[0], ", ", tof_c[1], ")"));
echo(str("Sensor face above any rim: ", sens_min, " mm (plate on rim, no feet)"));
echo(str("Collar plate radius ", plate_r, "; small tabs r ", small_tab_ro, "; jar tabs r ", jar_tab_ro));
for (v = vessels) echo(str(v[0], ": tabs clear of rim? small-tab inner r ", small_tab_ro - tab_t,
    v[1] > small_bore ? str(" inside bore r ", v[1]/2, " (slop ", v[1]/2 - small_tab_ro, ")") : " = locating",
    "; jar-tab inner r ", jar_tab_ro - tab_t, " vs rim outer r ", v[2]/2,
    (v[2]/2 < jar_tab_ro - tab_t - 3 || v[1]/2 > jar_tab_ro) ? " OK" : " CLASH"));
echo(str("Probe notch clear of box front: ", notch_r0 > front_edge_dist ? "yes" : "NO",
         " (notch starts ", notch_r0, ", box front at ", front_edge_dist, ")"));
assert(notch_r0 > front_edge_dist, "probe notch would be under the box");
echo(str("Counterweight pocket holds ", cw_coins, " US quarters = ", cw_coins*5.67, " g"));
assert(notch_w >= probe_tip_od + 1, "notch too narrow for heat-shrunk tip");

// ---------------- helpers ----------------
module rrect(x, y, h, r) {
    translate([r, r, 0]) linear_extrude(h) offset(r) square([x - 2*r, y - 2*r]);
}
module standoff(p, h) {
    translate([p[0], p[1], -eps]) difference() {
        cylinder(d = so_d, h = h + eps);
        // v5: pilot is cut once, from pilot_bottom_z, in base() (avoids coincident pilot walls)
    }
}
module lug_shape(l, z0, h) hull() {
    translate([l[0], l[1], z0]) cylinder(d = lug_d, h = h);
    translate([l[0] + (l[0] < 0 ? lug_off + 2 : -lug_off - 2),
               l[1] + (l[1] < 0 ? lug_off + 2 : -lug_off - 2), z0]) cylinder(d = 2, h = h);
}
module footprint2d() {
    translate([-wall, -wall]) offset(corner_r) offset(-corner_r) square([OX, OY]);
    for (l = lugs) hull() {
        translate(l) circle(d = lug_d);
        translate([l[0] + (l[0] < 0 ? lug_off + 2 : -lug_off - 2),
                   l[1] + (l[1] < 0 ? lug_off + 2 : -lug_off - 2)]) circle(d = 2);
    }
}

// ---------------- BASE ----------------
pilot_bottom_z = -0.6;   // v5: M2 pilot bottom (interior z; floor top = 0, underside = -2). [DERIVED from
                         // verified post/pilot geometry + M2 kit lengths 4/6/8 mm (no M2x5); see CHANGELOG-v5.md]
module base() {          // v5: v4 base with the 12 standoff pilots deepened into the floor
    difference() {
        base_v4();
        for (h = f_holes) translate([f_xy(h)[0], f_xy(h)[1], pilot_bottom_z]) cylinder(d = so_pilot, h = 6);
        for (h = t_holes) translate([t_xy(h)[0], t_xy(h)[1], pilot_bottom_z]) cylinder(d = so_pilot, h = 8);
        for (h = d_holes) translate([d_xy(h)[0], d_xy(h)[1], pilot_bottom_z]) cylinder(d = so_pilot, h = 6);
    }
}
module base_v4() {
    difference() {
        union() {
            translate([-wall, -wall, -floor_t]) rrect(OX, OY, base_h, corner_r);
            for (l = lugs) lug_shape(l, -floor_t, base_h - lid_lug_drop);
            if (cw_on) hull() {   // counterweight pod + arm to the front-left lug
                translate([cw_c[0], cw_c[1], -floor_t]) cylinder(d = cw_pocket_d + 2*cw_wall, h = cw_h);
                translate([lugs[0][0], lugs[0][1], -floor_t]) cylinder(d = lug_d, h = cw_h);
            }
        }
        if (cw_on) translate([cw_c[0], cw_c[1], -floor_t + cw_floor]) difference() {
            cylinder(d = cw_pocket_d, h = cw_h);
            for (a = [90, 210, 330]) rotate([0, 0, a])
                translate([cw_pocket_d/2 - cw_rib, -0.6, -1]) cube([2, 1.2, cw_h + 2]);
        }
        cube([IX, IY, ih + 1]);
        for (l = lugs) translate([l[0], l[1], -floor_t - 1]) {
            cylinder(d = screw_clear, h = base_h + 2);
            cylinder(d = screw_head_d, h = screw_head_h + 1);
        }
        translate([tof_c[0], tof_c[1], -floor_t - 1]) cylinder(d = win_d, h = floor_t + 2);
        translate([tof_c[0], tof_c[1], -floor_t - eps])
            cylinder(d1 = win_d + 2*win_chamfer, d2 = win_d, h = win_chamfer);
        translate([-wall - 1, usb_c[0], usb_c[1]]) rotate([0, 90, 0])
            linear_extrude(wall + 2) offset(2) square([usb_cut_h - 4, usb_cut_w - 4], center = true);
        translate([probe_x, -wall - 1, probe_z]) rotate([-90, 0, 0]) cylinder(d = probe_d, h = wall + 2);
        translate([probe_x - probe_d/2, -wall - 1, probe_z]) cube([probe_d, wall + 2, ih]);
    }
    for (h = f_holes) standoff(f_xy(h), f_so_h);
    for (h = t_holes) standoff(t_xy(h), t_so_h);
    for (h = d_holes) standoff(d_xy(h), d_so_h);
    translate([probe_x - 4, 2.5, -eps]) difference() {
        cube([8, 4, zt_h + 1.6]);
        translate([-1, (4 - zt_w)/2, 0]) cube([10, zt_w, zt_h]);
    }
}

// ---------------- LID ---------------- (modelled in assembly position: underside at z=ih)
module lid() {
    tongue_z = probe_z - probe_d/2 + probe_cable_d - clamp_crush;  // bottom of clamp tongue
    difference() {
        union() {
            translate([-wall, -wall, ih]) rrect(OX, OY, lid_t, corner_r);
            translate([fit, fit, ih - lip_h]) difference() {
                cube([IX - 2*fit, IY - 2*fit, lip_h + eps]);
                translate([lip_t, lip_t, -1]) cube([IX - 2*fit - 2*lip_t, IY - 2*fit - 2*lip_t, lip_h + 2]);
            }
            for (l = lugs) lug_shape(l, ih - lid_lug_drop, lid_lug_drop + lid_t);
            translate([probe_x - probe_d/2 + 0.2, -wall + 0.2, tongue_z])
                cube([probe_d - 0.4, wall - 0.2 + lip_t + fit, ih - tongue_z + eps]);
            // guide tubes under reset, boot and NeoPixel
            for (p = [f_reset, f_boot, f_neo]) translate([f_xy(p)[0], f_xy(p)[1], guide_bot])
                cylinder(d = guide_od + (p == f_neo ? 0.4 : 0), h = ih - guide_bot + eps);
        }
        for (l = lugs) translate([l[0], l[1], ih - lid_lug_drop - 1]) cylinder(d = screw_pilot, h = lid_lug_drop + lid_t);
        // poke holes (through lid + tube), 45 deg lead-in chamfer at the outside
        for (p = [f_reset, f_boot]) translate([f_xy(p)[0], f_xy(p)[1], 0]) {
            translate([0, 0, guide_bot - 1]) cylinder(d = poke_d, h = ih + lid_t);
            translate([0, 0, ih + lid_t - 0.8]) cylinder(d1 = poke_d, d2 = poke_d + 1.6 + eps, h = 0.8 + eps);
        }
        // NeoPixel light chimney + hole or skin
        translate([f_xy(f_neo)[0], f_xy(f_neo)[1], guide_bot - 1]) {
            cylinder(d = led_pipe_id, h = ih - guide_bot + 1 + (led_mode == "hole" ? 0 : lid_t - led_skin));
            if (led_mode == "hole") cylinder(d = led_hole_d, h = ih + lid_t);
        }
    }
}

// ---------------- COLLAR ---------------- (assembly position: top face at z=-floor_t)
module collar() {
    zt = -floor_t;
    zb = zt - col_t;   // plate underside = rim contact plane
    difference() {
        union() {
            // flat plate = box footprint + disc covering the largest rim
            translate([0, 0, zb]) linear_extrude(col_t) union() {
                footprint2d();
                translate(tof_c) circle(r = plate_r);
            }
            // centring tabs (overlap 1 mm into the plate)
            for (s = [[small_tab_ro, small_tab_ang], [jar_tab_ro, jar_tab_ang]])
                for (a = s[1]) translate([tof_c[0], tof_c[1], zb - tab_len])
                    rotate([0, 0, a - tab_arc/2]) rotate_extrude(angle = tab_arc)
                        translate([s[0] - tab_t, 0]) square([tab_t, tab_len + 1]);
        }
        translate([tof_c[0], tof_c[1], zb - tab_len - 1]) cylinder(d = col_win_d, h = col_t + tab_len + 2);
        // corner screws: M3 countersunk from below, flush with the rim plane
        for (l = lugs) translate([l[0], l[1], zb - 1]) {
            cylinder(d = screw_clear, h = col_t + 2);
            translate([0, 0, 1 - eps]) cylinder(d1 = cs_d, d2 = screw_clear, h = (cs_d - screw_clear)/2);
        }
        // probe notch, radial toward -Y, open to the plate edge
        translate([tof_c[0], tof_c[1], zb - tab_len - 1]) hull() {
            translate([0, -notch_r0 - notch_w/2, 0]) cylinder(d = notch_w, h = col_t + tab_len + 2);
            translate([-notch_w/2, -plate_r - 5, 0]) cube([notch_w, 1, col_t + tab_len + 2]);
        }
    }
}

// ---------------- ghosts for preview ----------------
module ghosts() {
    color("DarkSlateGray") translate([f_org[0], f_org[1], f_so_h]) cube([f_len, f_wid, pcb_t]);
    color("Silver") translate([f_org[0] - f_usb_overhang, f_org[1] + f_usb_y - f_usb_w/2, f_so_h + pcb_t])
        cube([7.3, f_usb_w, f_usb_h]);
    color("DimGray") translate([f_org[0] + 33, f_org[1] + 4.5, f_so_h + pcb_t]) cube([15.4, 14, f_parts_h]);
    color("Black") translate([t_org[0], t_org[1], t_so_h]) cube([t_len, t_wid, pcb_t]);
    color("White") for (x = t_qt_x) translate([t_org[0] + x - 2, t_org[1] + t_wid/2 - 3, t_so_h - t_parts_h])
        cube([4, 6, t_parts_h]);
    color("Red") translate([tof_c[0] - 2.2, tof_c[1] - 1.2, t_so_h - t_sensor_h]) cube([4.4, 2.4, t_sensor_h]);
    color("Black") translate([d_org[0], d_org[1], d_so_h]) cube([d_len, d_wid, pcb_t]);
    color("Green") let(c = d_xy(d_term))
        translate([c[0] - d_term_ext[0]/2, c[1] - 3.2, d_so_h + pcb_t]) cube([d_term_ext[0], d_term_ext[1], d_h - pcb_t]);
}
module beaker_ghost(gap) {
    rim_z = -floor_t - col_t - gap;
    color("LightCyan", 0.35) translate([tof_c[0], tof_c[1], rim_z - bk_h]) difference() {
        cylinder(d = vessel == 2 ? bk_rim_od + 6 : bk_rim_od, h = bk_h - (vessel == 2 ? 14 : 0));
        translate([0, 0, 3]) cylinder(d = bk_bore, h = bk_h);
    }
    if (vessel == 2) color("LightCyan", 0.35) translate([tof_c[0], tof_c[1], rim_z - 14.01]) difference() {
        cylinder(d = bk_rim_od, h = 14);   // jar neck / rim
        translate([0, 0, -1]) cylinder(d = bk_bore, h = 16);
    }
    // heat-shrunk probe tip in the notch
    color("LightGray") translate([tof_c[0], tof_c[1] - notch_r0 - notch_w/2, rim_z - 45])
        cylinder(d = probe_tip_od, h = 60);
}

// ---------------- output ----------------
if (part == "base") translate([0, 0, floor_t]) base();                           // underside on bed
else if (part == "lid") translate([0, 0, ih + lid_t]) rotate([180, 0, 0]) lid(); // top on bed
else if (part == "collar") translate([0, 0, -floor_t]) rotate([180, 0, 0]) collar(); // plate top on bed, tabs up
else if (part == "base_asm") base();          // assembly position (for CG checks)
else if (part == "lid_asm") lid();
else if (part == "collar_asm") collar();
else if (part == "assembly") {
    color("Gold", 0.9) base();
    ghosts();
    color("SteelBlue", 0.45) translate([0, 0, 25]) lid();
    color("Orange", 0.9) translate([0, 0, -12]) collar();
    translate([0, 0, -12]) beaker_ghost(0);
}
else if (part == "print") {
    translate([0, 0, floor_t]) base();
    translate([0, -OY - 45, ih + lid_t]) rotate([180, 0, 0]) translate([0, -IY, 0]) lid();
    translate([OX + 45, -30, -floor_t]) rotate([180, 0, 0]) translate([0, -IY, 0]) collar();
}
