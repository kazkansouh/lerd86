/*
 Copyright (C) 2018 Karim Kanso. All Rights Reserved.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

include <params.scad>
use <rounding.scad>

module leds() {
    for (i = [0:7]) {
        rotate([270,0,0])
        translate([hob_spacing*i - hob_spacing*3.5,0,0])
          cylinder(r=hob_radius, h=case_thickness+2, $fn=20);
    }
}

module cutouts() {
    // micro usb clearence
    translate([inside_width/2 - 1, -pcb_depth/2 -usb_port_oversize_width + usb_port_inset,pcb_base_z_offset + pcb_height + usb_port_standoff - usb_port_oversize_height + usb_port_height + usb_port_oversize_height*2])
        rotate([0,90,0])
      rounded_cube(2, [usb_port_height + usb_port_oversize_height*2, usb_port_width + usb_port_oversize_width*2, case_thickness + 2], $fn=10);

    // wifi module antanna clearence
    translate([-pcb_width/2 - wifi_module_oversize + wifi_module_inset,-inside_depth/2 - case_thickness - 1,pcb_base_z_offset + pcb_height + wifi_module_standoff - wifi_module_oversize])
        cube([wifi_module_width + wifi_module_oversize*2, case_thickness + 2, wifi_module_height + wifi_module_oversize*2]);

    // led clearence
    translate([0,inside_depth/2-1,pcb_base_z_offset + pcb_height + led_standoff])
        leds();
}

module clip() {
    translate([-case_thickness*1.5,-clip_width/2,0])
    difference() {
        rounded_cube(rounding, [case_thickness*3, clip_width, top_outside_height + clip_overhang], $fn=20);
        translate([-case_thickness*0.5,-0.5,-0.5])
        cube([case_thickness*2, clip_width+1, top_outside_height + clip_overhang + 1]);
    };

    translate([0,0,case_thickness/1.5])
    rotate([90,0,0])
    cylinder(r=case_thickness/1.5,h=clip_width-0.5, center=true, $fn=8);
};

module top() {
    difference() {
        translate([0,0,top_outside_height/2 + bottom_outside_height/2])
            rounded_cube(rounding, [outside_width, outside_depth, top_outside_height], center=true, $fn=20);
        translate([0,0,top_outside_height/2 + bottom_outside_height/2])
        translate([0,0,-case_thickness/2 - 0.5])
            cube([inside_width, inside_depth, top_inside_height + 1], center=true);
        cutouts();
        for (b = buttons) {
            translate([-pcb_width/2 + b[0], -pcb_depth/2 + b[1],pcb_base_z_offset + pcb_height + top_clearence - 0.5])
            cylinder(r=button_hole_radius, h=case_thickness+1, $fn=10);
        };
        for (d = [0:3:9]) {
            translate([pcb_width/2 - 23 - 2, -pcb_depth/2-1 + d + 3, pcb_base_z_offset + pcb_height + top_clearence - 0.5])
            rounded_cube(0.25, [23,1,case_thickness + 1]);
        };
    };

    for (z = [-1,1]) {
        translate([z * (pcb_width/2 + support_hole_radius),
                  z * (pcb_depth/2 + support_hole_radius),
                  0]) {
            translate([0,0,pcb_base_z_offset + pcb_height])
              cylinder(r=support_hole_diameter, h=top_clearence, $fn=20);
       };
    };

    for (b = buttons) {
        translate([-pcb_width/2 + b[0], -pcb_depth/2 + b[1],pcb_base_z_offset + pcb_height + button_top_standoff])
        difference() {
          cylinder(r=button_hole_radius + 1, h=top_clearence - button_top_standoff, $fn=10);
          translate([0, 0, - 0.5])
          cylinder(r=button_hole_radius, h=top_clearence - button_top_standoff + 1, $fn=10);
        };
    };

translate([outside_width/2, 0, 0])
    clip();

translate([-outside_width/2, 0, 0])
    rotate([0,0,180])
    clip();

};

rotate([0,180,0])
  top();
