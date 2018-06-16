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

module rounding(r,h) {
    difference() {
        cube([r+1, r+1, h+1]);
        translate([0,0,-1])
        cylinder(r=r, h=h+2);
    };
};

module rounded_cube(r,x) {
    difference() {
        cube(x);
        translate([r,r,-0.5])
        rotate([0,0,180])
            rounding(r,x[2]);
        translate([r,x[1] - r,-0.5])
        rotate([0,0,90])
            rounding(r,x[2]);
        translate([x[0] - r,r,-0.5])
        rotate([0,0,270])
            rounding(r,x[2]);
        translate([x[0] - r, x[1] - r,-0.5])
            rounding(r,x[2]);
    };
};
