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

# Apply TinyUSB patches needed for multicore MIDI clock filtering
# This is idempotent - patches are only applied if not already applied
TINYUSB_PATCH_APPLIED := $(shell cd $(TOP)/lib/tinyusb && grep -q "usb_midi_clock_count" src/class/midi/midi_device.c 2>/dev/null && echo "applied" || echo "")
ifeq ($(TINYUSB_PATCH_APPLIED),)
$(info Applying TinyUSB patches for board $(BOARD)...)
APPLY_PATCHES := $(shell cd $(TOP)/lib/tinyusb && git apply $(TOP)/ports/raspberrypi/boards/$(BOARD)/patches/*.patch 2>/dev/null || true)
endif
