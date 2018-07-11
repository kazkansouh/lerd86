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
use <top.scad>

module bottom() {
    difference() {
        rounded_cube(rounding, [outside_width, outside_depth, bottom_outside_height], center=true, $fn=20);
        translate([0,0,case_thickness/2 + 0.5])
            cube([inside_width, inside_depth, bottom_inside_height + 1], center=true);
        cutouts();
        top();
    };

    for (x = [-1,1]) {
        for (y = [-1,1]) {
            translate([x * (pcb_width/2 + support_hole_radius),
                      y * (pcb_depth/2 + support_hole_radius),
                      0]) {
                translate([0,0,-bottom_outside_height/2 + case_thickness])
                    cylinder(r=support_hole_diameter, h=base_clearence, $fn=20);
                translate([0,0,pcb_base_z_offset])
                    cylinder(r1=support_hole_radius, r2=support_hole_radius*0.8, h=pcb_height, $fn=20);
            };
        };
    };
};

//%top();
bottom();