/* Flash Split for 1M chips — MUST match src/elrs.flash.1m.ld byte-for-byte.
 * The egg and the full PteronautOS firmware share this layout so that a
 * U_FLASH swap between them lands at the same littlefs/eeprom/rfcal/wifi
 * addresses. Do NOT edit one without editing the other.
 */
/* sketch @0x40200000 (~967KB) (991216B) */
/* empty  @0x402F1FF0 (~4KB) (4112B) */
/* littlefs @0x402F3000 (~32KB) (32768B) */
/* eeprom @0x402FB000 (4KB) */
/* rfcal  @0x402FC000 (4KB) */
/* wifi   @0x402FD000 (12KB) */

MEMORY
{
  dport0_0_seg :                        org = 0x3FF00000, len = 0x10
  dram0_0_seg :                         org = 0x3FFE8000, len = 0x14000
  irom0_0_seg :                         org = 0x40201010, len = 0xF1ff0
}

PROVIDE ( _FS_start = 0x402F3000 );
PROVIDE ( _FS_end = 0x402FB000 );
PROVIDE ( _FS_page = 0x100 );
PROVIDE ( _FS_block = 0x1000 );
PROVIDE ( _EEPROM_start = 0x402fb000 );

INCLUDE "local.eagle.app.v6.common.ld"
