/*
 * Add a fill section after .text in order to force the linker to generate
 * thunks.
 */

SECTIONS {
  .text.fill : {
    FILL(0x00);
    . += 128M;
  }
} INSERT AFTER .text