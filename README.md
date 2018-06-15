# Nestronic Game Music Synthesizer Alarm Clock

![Nestronic](https://github.com/dkonigsberg/nestronic/blob/master/docs/images/nestronic-logo.png?raw=true)

## Introduction

_**Note: This project is still under development, and may have issues that prevent it from working correctly as-is.**_

A good overview of the project can be found in these blog posts:
* [Introducing the Nestronic](http://hecgeek.blogspot.com/2018/02/nestronic-1.html)
* [Nestronic System Architecture](http://hecgeek.blogspot.com/2018/03/nestronic-2.html)
* [Building the Nestronic Prototype](http://hecgeek.blogspot.com/2018/05/nestronic-3.html)

A few videos demonstrating the project are here:
* [First end-to-end test](https://www.youtube.com/watch?v=97jic_WRrwY)
* [DuckTails!](https://www.youtube.com/watch?v=eafaFr9Q_rU)
* [First prototype assembled and working!](https://www.youtube.com/watch?v=izMFPKmD5ZU)
* [Completed case, buttons, and Blaster Master!](https://www.youtube.com/watch?v=pXp97wzkQEE)

## Project Layout

### Hardware
The "hardware" directory contains [KiCad](http://kicad-pcb.org/) projects
with the complete circuit schematics, bill-of-materials, and
PCB layouts.

There are two circuit boards, the main board and the input board.
The main board contains the majority of the system, while the input
board handles buttons and anything else that needs to be placed on
the top shell of the enclosure.

### Software
The "software" directory contains all the source code for the firmware
that runs on the hardware. The "2a03" subdirectory contains code for
the RP2A03 CPU (a.k.a. NES APU), while the "esp32" subdirectory contains
code for the modern ESP32 microcontroller that drives the rest of the
system.

### Models
The "models" directory contains any CAD models and related resources
necessary to physically assemble the project. This may be sparse for now,
but will be filled out over time.

## Credits
ESP32 schematic symbols and footprints originally from:
https://github.com/adamjvr/ESP32-kiCAD-Footprints
