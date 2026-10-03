# Third-Party Code Notice

## Heartbeat Soundtracker player

`include/ultimate_hbplay_lib.h` / `.c` are a C port of the standalone
player of **Heartbeat Soundtracker**
(https://sites.google.com/view/heartbeatsoundtracker), (c) Aleksi Eeben /
Eight Bit Shed. `include/heartbeat/*.bin` are that player's own tempo and
frequency tables, unchanged.

The player source is normally distributed only to Heartbeat Soundtracker
licence holders (Composer's Licence / Collector's Edition). Xander Mol holds
a Gold licence and obtained written permission from the author (email
correspondence with Aleksi Eeben, 2026) to redistribute the player source
publicly and to create and publish an Oscar64 C library derived from it.
The C port was first published in heartbeat-demo
(https://github.com/xahmol/heartbeat-demo) and moved into this library in
1.4.0, together with the tables. The original assembly source is not part
of this repository.

Code ported or adapted from the original player carries a credit to Aleksi
Eeben / Heartbeat Soundtracker. Songs are not part of this library: use
your own compositions or songs you have the rights to distribute.

## Upic

The Upic module (`include/ultimate_upic_lib.h` / `.c`) is based on Aleksi
Eeben's Upic v1.3 source (`display.s`, `drawing.s`), published here with
his permission (2026-10-02/03); see `docs/UPIC_MANUAL.md`.
