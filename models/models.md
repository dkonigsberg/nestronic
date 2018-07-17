# Nestronic Enclosure Models

## Introduction

This directory contains all the assets necessary to produce the physical
enclosure for the Nestronic. This document explains how to use those
assets, and ultimately how to assemble the enclosure.

## Electronics

### Main Board

The assets for the main circuit board are contained within the "main-board"
project under the top-level "hardware" directory. This board is installed
into the base of the enclosure.

### Input Board

The assets for the input circuit board are contained within the "input-board"
project under the top-level "hardware" directory. This board is installed
into the top of the enclosure.

### Display Module

* Newhaven Display Graphic OLED
* Part number:
    NHD-3.12-25664UCW2 (white),
    NHD-3.12-25664UCB2 (blue), or
    NHD-3.12-25664UCY2 (yellow)
* 3.12" diagonal, 256x64 pixels
* Reference: http://www.newhavendisplay.com/nhd31225664ucw2-p-9583.html
* _**Note:** You will need to solder a 20-pin header onto the display module,
  and will also need to procure a short cable to connect it to a matching
  pin header on the main board._

### Speaker

* CUI Inc. Standard Size Speaker
* Part number: CSS-66668N
* 3.0 W, 8 Ohm, Neodymium Magnet, Paper Cone
* Reference: https://www.cui.com/product/audio/speakers/standard-(41-mm~205-mm)/css-66668n
* _**Note:** You will need to solder a cable with a JST-PH 2-pin plug onto
  the speaker's terminals. One convenient source for this is sold by
  Adafruit as a [battery extension cable](https://www.adafruit.com/product/1131)
  that you can cut the socket off of._

### Connector Cable

* JST Inc. Jumper Cable
* Part number: A06SR06SR30K152B
* JUMPER 06SR-3S - 06SR-3S 6"
* Digi-Key Part number: 455-3015-ND

## Mechanical Hardware

The mechanical hardware is specified in terms of parts that are orderable
from the [McMaster-Carr](https://www.mcmaster.com/) catalog. For the heat-set
inserts, it is probably best to stick to these exact parts. For the other
components, anything of similar specifications should work.

### Heat-Set Inserts
* **Heat-Set Inserts for Plastics**
    * M3 x 0.5 mm Thread Size, 6.4 mm Installed Length
    * Part: 94180A333
    * Quantity: 8
* **Heat-Set Inserts for Plastics**
    * M3 x 0.5 mm Thread Size, 3.8 mm Installed Length
    * Part: 94180A331
    * Quantity: 8
* **Heat-Set Inserts for Plastics**
    * M2.5 x 0.45 mm Thread Size, 3.4 mm Installed Length
    * Part: 94180A321
    * Quantity: 5

### Screws
* **Steel Pan Head Phillips Screw**
    * M3 x 0.5 mm Thread, 8 mm Long
    * Part: 92005A118
    * Quantity: 8
* **Steel Pan Head Phillips Screw**
    * M3 x 0.5 mm Thread, 6 mm Long
    * Part: 92005A116
    * Quantity: 8
* **Steel Phillips Flat Head Screws**
    * M2.5 x 0.45 mm Thread, 6 mm Long
    * Part: 91420A016
    * Quantity: 4
* **Steel Pan Head Phillips Screw**
    * M2.5 x 0.45 mm Thread, 5 mm Long
    * Part: 92005A061
    * Quantity: 1

### Miscellaneous
* **Zinc-Plated Steel Washer**
    * For M3 Screw Size, 3.2 mm ID, 7 mm OD
    * Part: 91166A210
    * Quantity: 8-12
* **Polyethylene Foam Adhesive-Back Bumper**
    * 1/2" OD, 1/8" High
    * Part: 8213K4
    * Quantity: 4
    * _**Note:** Nonslip rubber bumper 8771K81 may alternatively be used._

## 3D Printed Components

The prototype enclosure was printed on an [Ultimaker 3](https://ultimaker.com/en/products/ultimaker-3) using [Cura](https://ultimaker.com/en/products/ultimaker-cura-software) as the slicer.
The print bed was prepared with [Magigoo](https://magigoo.com/) to ensure
bottom layer adhesion during the printing process.
All parts, except for the thumbwheel, need a 90 degree rotation in Cura to
print with the correct orientation.

### Enclosure Shell
These components were printed using a standard 0.4mm AA nozzle, and will
probably look fine if printed with a lower level of detail than specified.

* **Base** (base.stl)
    * Extruder 1: Atomic Filament 2.85mm Gray PLA
    * Extruder 2: Ultimaker Breakaway Material
    * Layer Height: 0.1mm
    * Infill: 20%
    * Supports: yes (Extruder 2)
    * _**Note:** Using a prime tower is highly recommended as a way to reduce
      any oozing caused by the support material."_


* **Top** (top.stl, top-touchpad.stl)
    * Extruder 1: Atomic Filament 2.85mm Light Gray v2 PLA
    * Extruder 2: Proto-Pasta 2.85mm Conductive PLA
    * Layer Height: 0.1mm
    * Infill: 20%
    * Supports: yes (Extruder 1)
    * Print the top model with extruder 1.
    * Print the top-touchpad model with extruder 2 and a Y offset of
      -14.9938 mm.
    * _**Note:** The conductive PLA material can print on the messy side.
      Using a prime tower is highly recommended, however some residual mess
      is inevitable."_


* **Front Bezel** (bezel.stl)
    * Material: Ultimaker Red PLA
    * Layer Height: 0.1mm
    * Infill: 20%
    * Supports: no


* **Speaker Gasket** (top-speaker-gasket.stl)
    * Material: Ultimaker Black TPU 95A
    * Layer Height: 0.2mm
    * Infill: 20%
    * Supports: no


### Enclosure Fittings
These components were printed using a 0.25mm AA nozzle, and should ideally be
printed with the highest level of detail that your printer is capable of.

* **Thumbwheel** (thumbwheel.stl)
    * Material: Ultimaker Black PLA
    * Layer Height: 0.06mm
    * Infill: 20%
    * Supports: no

* **4-Way Button** (button_4way.stl)
    * Material: Ultimaker Black PLA
    * Layer Height: 0.06mm
    * Infill: 100%
    * Supports: no

* **Start/Select Button** (button_select.stl)
    * Print quantity: 2
    * Material: Ultimaker Black PLA
    * Layer Height: 0.06mm
    * Infill: 100%
    * Supports: no

* **A/B Buttons** (button_ab.stl)
    * Print quantity: 2
    * Material: Ultimaker Red PLA
    * Layer Height: 0.06mm
    * Infill: 100%
    * Supports: no

## Front Panel

The front panel was ordered from [Beta Layout](https://us.beta-layout.com/frontpanel/)
in clear acrylic, using their provided design software. The data file is also located in this directory ("front-panel.T3000").
