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

pcb_width=62.4;
pcb_depth=42.7;
pcb_height = 1.2;

support_hole_diameter=1.8;
support_hole_radius = support_hole_diameter/2;

usb_port_width = 7.8;
usb_port_height = 2.8;
usb_port_standoff = 1.5;
usb_port_inset = 0.2;
usb_port_oversize_width = 1.6;
usb_port_oversize_height = 2.1;

wifi_module_width = 16;
wifi_module_height = 0.78;
wifi_module_standoff = 3;
wifi_module_inset = 11.65;
wifi_module_oversize = 0.25;

padding=3.25;

case_thickness = 2;
rounding = 2;

inside_width = pcb_width + support_hole_diameter * 2 + padding * 2;
inside_depth = pcb_depth + support_hole_diameter * 2 + padding * 2;

outside_width = inside_width + case_thickness * 2;
outside_depth = inside_depth + case_thickness * 2;

base_clearence = 4;
base_above_clearence = 3.6;

bottom_inside_height = base_clearence + pcb_height + base_above_clearence;
bottom_outside_height = bottom_inside_height + case_thickness;

pcb_base_z_offset = bottom_outside_height/2 - pcb_height - base_above_clearence;

top_clearence = 20;

top_inside_height = top_clearence - base_above_clearence;
top_outside_height = top_inside_height + case_thickness;

hob_radius = 1.65;
hob_spacing = 7.62;

// minimum 3.4
led_standoff = base_above_clearence;
if (led_standoff < 3.4) {
    color("magenta") %text("ERROR!",size=50);
    echo("ERROR: led_standoff less than 3.4");
}


button1_x = 4.4;
button1_y = 8.4;

button2_x = button1_x + 19*2.54;
button2_y = button1_y + 5*2.54;

buttons = [[button1_x, button1_y],
           [button2_x, button2_y]];

button_hole_radius = 1.2;
button_top_standoff = 7;

clip_width = 10;
clip_overhang = bottom_outside_height/2;