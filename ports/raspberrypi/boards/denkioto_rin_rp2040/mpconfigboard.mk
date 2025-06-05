USB_VID = 0x239A
USB_PID = 0x80F4
USB_PRODUCT = "rin"
USB_MANUFACTURER = "denki-oto"

CHIP_VARIANT = RP2040
CHIP_FAMILY = rp2

EXTERNAL_FLASH_DEVICES = "W25Q64JVxQ"

# Board-specific multicore functionality
SRC_C += boards/$(BOARD)/denkioto/multicore.c
SRC_C += boards/$(BOARD)/denkioto/rin.c
SRC_QSTR += boards/$(BOARD)/denkioto/rin.c
