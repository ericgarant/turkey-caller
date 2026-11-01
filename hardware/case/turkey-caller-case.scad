// Turkey caller case: printable base, lid, gasket and button clamps.
//
// Sized for the parts already on the breadboard (Freenove kit):
//   - four "big push buttons": 12 x 12 mm tactile switches with square colour caps
//   - LCD1602 with the I2C chip on its own board (GJD 1602IIC), 80 x 36 mm board
//   - Freenove ESP32-WROVER board (USB-C) on a perfboard with header sockets
//
// All sizes in mm. Values marked MEASURE are typical sizes for these parts,
// not measured on Eric's own parts: check them with a ruler or calipers first.
//
// Render one part at a time (see README.md):
//   openscad -D 'part="base"' -o base.stl turkey-caller-case.scad
// part = base | lid | gasket | clamp | fit_test | assembly

part = "assembly";

/* [Box] */
box_w = 150;        // outside, left to right
box_d = 100;        // outside, front to back
base_h = 50;        // outside height of the base, without the lid
wall = 4;           // thick enough to hold the gasket groove
floor_t = 2.4;
corner_r = 8;       // outside corner radius
lid_t = 3;

/* [Lid screws and gasket] */
screw_inset = 7;    // screw centre from the outside edge
screw_hole = 3.4;   // M3 clearance in the lid
screw_head = 6.2;   // M3 head, counterbored in the lid
screw_pilot = 2.6;  // M3 self-tapping into the post (4.0 for a heat-set insert)
post_d = 10;
groove_w = 1.8;     // gasket groove in the rim
groove_depth = 1.5;
gasket_h = 2.2;     // TPU gasket height: sticks out 0.7 mm to be squeezed

/* [Kit push buttons] */
sw_body = 12.0;     // MEASURE: switch body, square
sw_top_h = 11.5;    // MEASURE: from the switch's bottom face to the top of its cap
cap = 12.0;         // MEASURE: square cap
protrude = 2.0;     // how far the caps stand above the lid
fit = 0.4;          // clearance around the body and the cap
sleeve_t = 1.6;     // sleeve wall under the lid
clamp_w = 7;        // clamp bar width: fits between the legs (legs point left and right)
clamp_t = 2.5;
clamp_screw = 11;   // clamp screw distance from the button centre (M2 x 6 self-tapping)

/* [LCD: GJD 1602IIC] */
lcd_pcb = [80, 36];
lcd_holes = [75, 31];   // MEASURE: mounting hole spacing
lcd_frame_h = 7.5;      // MEASURE: height of the metal frame above the circuit board
lcd_view = [66, 17];    // window cut through the lid (visible area 64.5 x 16)
window_plate = [72, 24, 2]; // clear acrylic window glued in a recess (thickness 0 = no window)

/* [Lid layout] (centres, measured from the lid's top-left corner seen from above) */
lcd_center = [75, 24];
yellow = [27.5, 49];
blue = [27.5, 72];
green = [87.5, 61];
red = [130, 61];
label = [75, 90, 122, 13];  // centre x, centre y, length, height: 12 mm P-touch tape
label_depth = 0.4;

/* [Inside the base] (measured from the inside front-left corner) */
perf = [90, 70];          // perfboard
perf_pos = [50, 11];      // right-aligned, ESP32's USB-C end at the right wall
perf_holes_inset = 2.5;   // MEASURE: mounting hole centre from the perfboard edges
standoff_h = 6;
cell_holder = [21, 78];   // MEASURE: 18650 holder footprint
cell_pos = [27, 8];

/* [Left wall: power] (along the wall from the front, height from the floor) */
switch_d = 12.2;          // MEASURE: round toggle switch (0 to skip)
switch_pos = [25, 30];
boot_d = 5;               // pen-tip hole for the recessed BOOT button
boot_pos = [45, 30];
charge_usbc = [9.6, 3.8];
charge_pos = [70, 12];    // MEASURE after fixing the charge board against the wall

/* [Right wall: ESP32 USB-C for flashing] */
board_usbc = [9.6, 3.8];
board_usb_pos = [50, 17]; // MEASURE once the board sits on the perfboard
plug_recess = [13, 7.5, 2.5]; // outside pocket so a cable plug reaches the port through the wall

$fn = 64;

// ---------- helpers ----------
module rounded_box(w, d, h, r) {
  hull() for (x = [r, w - r], y = [r, d - r]) translate([x, y, 0]) cylinder(r = r, h = h);
}
// a closed band following the rounded outline, centred `inset` from the outside edge
module ring_path(w, d, r, inset, width, h) {
  difference() {
    translate([inset - width / 2, inset - width / 2, 0])
      rounded_box(w - 2 * inset + width, d - 2 * inset + width, h, max(r - inset + width / 2, 1));
    translate([inset + width / 2, inset + width / 2, -1])
      rounded_box(w - 2 * inset - width, d - 2 * inset - width, h + 2, max(r - inset - width / 2, 1));
  }
}
// a rounded slot through a wall along x
module x_slot(size, depth) {
  r = min(size[0], size[1]) / 2;
  rotate([0, 90, 0]) hull()
    for (dy = [-(size[0] / 2 - r), size[0] / 2 - r])
      translate([0, dy, 0]) cylinder(r = r, h = depth, center = true);
}
module x_hole(d, depth) { rotate([0, 90, 0]) cylinder(d = d, h = depth, center = true); }

screw_xy = [for (x = [screw_inset, box_w - screw_inset], y = [screw_inset, box_d - screw_inset]) [x, y]];
function lid_xy(p) = [p[0], box_d - p[1]]; // layout is given from the top edge
buttons = [yellow, blue, green, red];

// button geometry: z = 0 is the lid's underside, the lid's top is lid_t
sw_base_z = lid_t + protrude - sw_top_h;  // the switch's bottom face (negative: below the lid)
sleeve_in = sw_body + fit;
sleeve_out = sleeve_in + 2 * sleeve_t;

// ---------- base ----------
module base() {
  in_x = wall; in_y = wall;
  difference() {
    union() {
      difference() {
        rounded_box(box_w, box_d, base_h, corner_r);
        translate([wall, wall, floor_t]) rounded_box(box_w - 2 * wall, box_d - 2 * wall, base_h, corner_r - wall);
      }
      for (p = screw_xy) translate([p[0], p[1], 0]) cylinder(d = post_d, h = base_h);
      for (x = [perf_holes_inset, perf[0] - perf_holes_inset], y = [perf_holes_inset, perf[1] - perf_holes_inset])
        translate([in_x + perf_pos[0] + x, in_y + perf_pos[1] + y, 0]) cylinder(d = 6, h = floor_t + standoff_h);
      // two low rails keep the 18650 holder in place (tie it down or glue it too)
      for (dx = [-1.6, cell_holder[0]])
        translate([in_x + cell_pos[0] + dx, in_y + cell_pos[1], 0]) cube([1.6, cell_holder[1], floor_t + 5]);
      // boss behind the BOOT hole: glue a 6 x 6 mm tactile switch on its inner face,
      // 4 mm inside the wall, so only a pen tip reaches it
      translate([wall, in_y + boot_pos[0] - 6, floor_t + boot_pos[1] - 6]) cube([4, 12, 12]);
    }
    for (p = screw_xy) translate([p[0], p[1], base_h - 14]) cylinder(d = screw_pilot, h = 15);
    for (x = [perf_holes_inset, perf[0] - perf_holes_inset], y = [perf_holes_inset, perf[1] - perf_holes_inset])
      translate([in_x + perf_pos[0] + x, in_y + perf_pos[1] + y, floor_t]) cylinder(d = 2.2, h = standoff_h + 1);
    translate([0, 0, base_h - groove_depth]) ring_path(box_w, box_d, corner_r, wall / 2, groove_w, groove_depth + 1);
    // left wall: switch, BOOT, charging port
    if (switch_d > 0) translate([wall / 2, in_y + switch_pos[0], floor_t + switch_pos[1]]) x_hole(switch_d, wall + 2);
    translate([wall / 2 + 2, in_y + boot_pos[0], floor_t + boot_pos[1]]) x_hole(boot_d, wall + 10);
    translate([wall / 2, in_y + charge_pos[0], floor_t + charge_pos[1]]) x_slot(charge_usbc, wall + 2);
    // right wall: the ESP32's own USB-C
    translate([box_w - wall / 2, in_y + board_usb_pos[0], floor_t + board_usb_pos[1]]) x_slot(board_usbc, wall + 2);
    translate([box_w - plug_recess[2] / 2 + 0.01, in_y + board_usb_pos[0], floor_t + board_usb_pos[1]])
      x_slot([plug_recess[0], plug_recess[1]], plug_recess[2]);
  }
}

// ---------- lid (modelled top face up; part="lid" flips it for printing) ----------
module lid() {
  sh = lcd_frame_h + max(window_plate[2] - (lid_t - 1.2), 0);
  lc = lid_xy(lcd_center);
  difference() {
    union() {
      rounded_box(box_w, box_d, lid_t, corner_r);
      for (sx = [-1, 1], sy = [-1, 1])
        translate([lc[0] + sx * lcd_holes[0] / 2, lc[1] + sy * lcd_holes[1] / 2, -sh]) cylinder(d = 5.5, h = sh + 0.01);
      for (b = buttons) let(c = lid_xy(b)) {
        // square sleeve that holds the switch body
        translate([c[0] - sleeve_out / 2, c[1] - sleeve_out / 2, sw_base_z]) cube([sleeve_out, sleeve_out, -sw_base_z + 0.01]);
        // two posts for the clamp screws
        for (s = [-1, 1]) translate([c[0], c[1] + s * clamp_screw, sw_base_z]) cylinder(d = 5, h = -sw_base_z + 0.01);
      }
    }
    // LCD window, the recess for the clear plate, and the standoff holes (M2.5)
    translate([lc[0] - lcd_view[0] / 2, lc[1] - lcd_view[1] / 2, -1]) cube([lcd_view[0], lcd_view[1], lid_t + 2]);
    if (window_plate[2] > 0)
      translate([lc[0] - window_plate[0] / 2, lc[1] - window_plate[1] / 2, -1]) cube([window_plate[0], window_plate[1], lid_t - 1.2 + 1]);
    for (sx = [-1, 1], sy = [-1, 1])
      translate([lc[0] + sx * lcd_holes[0] / 2, lc[1] + sy * lcd_holes[1] / 2, -sh - 1]) cylinder(d = 2.2, h = sh);
    for (b = buttons) let(c = lid_xy(b)) {
      translate([c[0] - sleeve_in / 2, c[1] - sleeve_in / 2, sw_base_z - 1]) cube([sleeve_in, sleeve_in, -sw_base_z + 1]);
      translate([c[0] - (cap + fit) / 2, c[1] - (cap + fit) / 2, -1]) cube([cap + fit, cap + fit, lid_t + 2]);
      for (s = [-1, 1]) translate([c[0], c[1] + s * clamp_screw, sw_base_z - 1]) cylinder(d = 1.6, h = 6);
    }
    translate([label[0] - label[2] / 2, box_d - label[1] - label[3] / 2, lid_t - label_depth]) cube([label[2], label[3], 1]);
    for (p = screw_xy) {
      translate([p[0], p[1], -1]) cylinder(d = screw_hole, h = lid_t + 2);
      translate([p[0], p[1], lid_t - 1.6]) cylinder(d = screw_head, h = 2);
    }
  }
}

// ---------- button clamp (print 4): a bar under the switch, between its legs ----------
module clamp() {
  difference() {
    hull() for (s = [-1, 1]) translate([0, s * clamp_screw, 0]) cylinder(d = clamp_w, h = clamp_t);
    for (s = [-1, 1]) translate([0, s * clamp_screw, -1]) cylinder(d = 2.3, h = clamp_t + 2);
  }
}

// ---------- gasket (print in TPU) ----------
module gasket() { ring_path(box_w, box_d, corner_r, wall / 2, groove_w - 0.3, gasket_h); }

// ---------- fit test: a corner of the lid with one button and one LCD standoff ----------
module fit_test() {
  intersection() {
    translate([0, box_d, lid_t]) rotate([180, 0, 0]) lid();
    translate([5, 15, -1]) cube([50, 50, 30]);
  }
}

// ---------- output ----------
if (part == "base") base();
else if (part == "lid") translate([0, box_d, lid_t]) rotate([180, 0, 0]) lid();
else if (part == "gasket") gasket();
else if (part == "clamp") for (i = [0 : 3]) translate([i * 12, 0, 0]) clamp();
else if (part == "fit_test") fit_test();
else {
  color("dimgray") base();
  color("black") translate([0, 0, base_h - groove_depth]) gasket();
  color("slategray", 0.9) translate([0, 0, base_h + 0.7]) lid();
}
