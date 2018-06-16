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

include <rounding.scad>

// set to true/false to show lid or case
makelid = true;

hobs_wide = 10;
hobs_deep = 20;
hob_radius = 1.65;
hob_spacing = 5.08;

plane_width = (hobs_wide+1)*hob_spacing;
plane_depth = (hobs_deep+1)*hob_spacing;

plane_height = 2;

oversize=12;
wall_width=2;

internal_height=60-plane_height;

module case() {
    difference() {
        rounded_cube(6, [plane_width+oversize*2+wall_width*2,plane_depth+oversize*2+wall_width*2,plane_height+internal_height], $fn=50);
        translate([wall_width,wall_width,plane_height])
            cube(size = [plane_width+oversize*2,plane_depth+oversize*2,internal_height+1]);

        translate([oversize+wall_width,oversize+wall_width,0])
        for (i = [1 : hobs_wide]) {
            for (j = [1 : hobs_deep]) {
                translate([i*hob_spacing, j*hob_spacing, -1])
                    cylinder(r1 = hob_radius,r2 = hob_radius,$fn = 12,h = plane_height+2, center=false);
            }
        }
    }
};

corner_width=12;

m3_diameter = 3.6;
m3_nut_diameter = 5.3;
m3_nut_diameter_horizontal = 6.1;
m3_nut_height = 2.2;
nut_height = m3_nut_height + 0.25;

points = [
  [0,0,0],
  [corner_width,0,0],
  [0,corner_width,0],
  [0,0,internal_height],
  [corner_width,0,internal_height],
  [0,corner_width,internal_height],
];

faces = [
  [0,1,2],
  [0,3,4,1],
  [0,2,5,3],
  [1,4,5,2],
  [3,5,4]
];

module corner() {
    difference() {
        polyhedron(points,faces);
        translate([corner_width/4,corner_width/4,plane_height])
            cylinder(h = internal_height+2, r=m3_diameter/2 + 0.1, $fn=10);
#        translate([corner_width/4,corner_width/4, internal_height - 5])
        rotate([0,0,-15])
            cylinder(h = nut_height, r=m3_nut_diameter_horizontal/2, $fn=6);
#        translate([corner_width/4 + sin(45) * 3, corner_width/4 + sin(45) * 3, internal_height - 0.25 - 5])
        rotate([0,0,-15])
            cylinder(h = nut_height + 0.5, r=m3_nut_diameter_horizontal/2 + 0.5, $fn=6);
    };
};


module bottom() {
    case();
    translate([wall_width,wall_width,plane_height])
        corner();

    translate([wall_width,plane_depth+oversize*2+wall_width,plane_height])
        rotate(270)
        corner();
    translate([plane_width+oversize*2+wall_width,wall_width,plane_height])
        rotate(90)
        corner();
    translate([plane_width+oversize*2+wall_width,plane_depth+oversize*2+wall_width,plane_height])
        rotate(180)
        corner();
};

tee_width = 20;
tee_height = 7;
tee_depth = 15;
tee_support_width = 5;

tee_angle = atan(tee_height/(tee_depth-wall_width));
tee_depth_alt = tee_height/sin(tee_angle);
module tee() {
    union() {
        cube([wall_width,tee_width,tee_height]);

        for (y = [0,tee_width -tee_support_width]) {
            translate([wall_width,y,0])
            difference() {
                cube([tee_depth-wall_width,tee_support_width,tee_height]);

                translate([tee_depth-wall_width,0,0])
                    rotate([0,tee_angle,0])
                    translate([-tee_depth_alt,-1,0])
                    cube([tee_depth_alt,tee_support_width+2,tee_height]);
            };
        };
    };
};

module lidcorner() {
    translate([corner_width/4,corner_width/4,-1])
            cylinder(h = plane_height+2, r=m3_diameter/2 + 0.1, $fn=10);
}

module lid() {
    difference() {
        rounded_cube(6, [plane_width+oversize*2+wall_width*2,plane_depth+oversize*2+wall_width*2,plane_height], $fn=50);
        translate([wall_width,wall_width,0])
            lidcorner();

        translate([wall_width,plane_depth+oversize*2+wall_width,0])
            rotate(270)
            lidcorner();
        translate([plane_width+oversize*2+wall_width,wall_width,0])
            rotate(90)
            lidcorner();
        translate([plane_width+oversize*2+wall_width,plane_depth+oversize*2+wall_width,0])
            rotate(180)
            lidcorner();
        //cable hole
        translate([plane_width/2+oversize+wall_width,plane_depth/4+oversize/2+wall_width/2,-1])
        cylinder(r=5, h=plane_height+2);
    };
    translate([wall_width,plane_depth/2+oversize+wall_width-tee_width/2,plane_height])
        tee();
    translate([plane_width+oversize*2+wall_width*2-wall_width,plane_depth/2+oversize+wall_width-tee_width/2+tee_width,plane_height])
        rotate(180)
        tee();
};

translate([-(plane_width/2)-oversize-wall_width,
           -(plane_depth/2)-oversize-wall_width,
           0]) {
    if (makelid) {
        lid();
    } else {
        bottom();
    }
};
