# BMS CAN Bootloader —— 裸机构建(不依赖 west/Zephyr)。
# 工具链沿用工程内的 arm-none-eabi-gcc。

GNUARMEMB_TOOLCHAIN_PATH ?= ../Tools/arm-gcc/xpack-arm-none-eabi-gcc-15.2.1-1.1
TOOLCHAIN ?= $(GNUARMEMB_TOOLCHAIN_PATH)/bin
CC   := $(TOOLCHAIN)/arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN)/arm-none-eabi-objcopy
SIZE := $(TOOLCHAIN)/arm-none-eabi-size

TARGET := bootloader
SRCS   := startup.c bl_main.c bl_flash.c bl_can.c bl_ota.c
LDSCRIPT := stm32f103c8.ld

# -Os + gc-sections 压体量;nano、无异常/无 libc 启动。
CFLAGS := -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections \
          -Wall -Wextra -std=c11 -fno-common -g
LDFLAGS := -T$(LDSCRIPT) -nostartfiles -nostdlib -Wl,--gc-sections \
           -Wl,-Map=$(TARGET).map

all: $(TARGET).bin $(TARGET).hex size

$(TARGET).elf: $(SRCS) $(LDSCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: $(TARGET).elf
	$(SIZE) $(TARGET).elf

clean:
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).map

.PHONY: all size clean
