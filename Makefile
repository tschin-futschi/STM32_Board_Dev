######################################################################
# STM32F429ZGT6 Motor Debug Board - Makefile
# Toolchain: arm-none-eabi-gcc (MSYS2/MinGW)
######################################################################

# Project name
TARGET = firmware

# Build directory
BUILD_DIR = build

# Fix temp dir permission issue on Windows (linker needs writable TEMP)
export TMPDIR := $(CURDIR)/$(BUILD_DIR)
export TMP    := $(CURDIR)/$(BUILD_DIR)
export TEMP   := $(CURDIR)/$(BUILD_DIR)

# Debug/Release
ifdef DEBUG
  OPT = -Og -g3 -DDEBUG
else
  OPT = -O2
endif

######################################################################
# Paths
######################################################################

SPL_DIR = STM32F4xx_DSP_StdPeriph_Lib_V1.9.0/Libraries
SPL_DRV_DIR = $(SPL_DIR)/STM32F4xx_StdPeriph_Driver
CMSIS_DEV_DIR = $(SPL_DIR)/CMSIS/Device/ST/STM32F4xx
CMSIS_INC_DIR = $(SPL_DIR)/CMSIS/Include

######################################################################
# Sources
######################################################################

# Core sources
C_SOURCES  = Core/Src/main.c
C_SOURCES += Core/Src/stm32f4xx_it.c
C_SOURCES += Core/Src/system_stm32f4xx.c

# BSP sources
C_SOURCES += BSP/Src/bsp_tick.c
C_SOURCES += BSP/Src/bsp_led.c
C_SOURCES += BSP/Src/bsp_uart.c
C_SOURCES += BSP/Src/bsp_i2c1.c
C_SOURCES += BSP/Src/bsp_i2c3.c
C_SOURCES += BSP/Src/bsp_pmic.c

# App sources
C_SOURCES += App/Src/app_protocol.c

# SPL driver sources (add as needed per phase)
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_rcc.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_gpio.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_usart.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_dma.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_i2c.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_exti.c
C_SOURCES += $(SPL_DRV_DIR)/src/stm32f4xx_syscfg.c
C_SOURCES += $(SPL_DRV_DIR)/src/misc.c

# Startup
ASM_SOURCES = Core/Startup/startup_stm32f429_439xx.s

######################################################################
# Toolchain
######################################################################

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

######################################################################
# Flags
######################################################################

# CPU
CPU_FLAGS = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb

# C defines
C_DEFS  = -DUSE_STDPERIPH_DRIVER
C_DEFS += -DSTM32F429_439xx
C_DEFS += -DHSE_VALUE=8000000U

# Include paths
C_INCLUDES  = -ICore/Inc
C_INCLUDES += -IBSP/Inc
C_INCLUDES += -IApp/Inc
C_INCLUDES += -I$(SPL_DRV_DIR)/inc
C_INCLUDES += -I$(CMSIS_DEV_DIR)/Include
C_INCLUDES += -I$(CMSIS_INC_DIR)

# Compiler flags
CFLAGS  = $(CPU_FLAGS) $(C_DEFS) $(C_INCLUDES) $(OPT)
CFLAGS += -ffunction-sections -fdata-sections -fno-common
CFLAGS += -Wall -Wextra -Wshadow
CFLAGS += -std=c99 -pipe

# Assembler flags
ASFLAGS = $(CPU_FLAGS) $(OPT) -pipe

# Linker
LDSCRIPT = Linker/STM32F429ZGTX_FLASH.ld
LDFLAGS  = $(CPU_FLAGS)
LDFLAGS += -T$(LDSCRIPT)
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -specs=nano.specs
LDFLAGS += -lc -lm -lnosys
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref

######################################################################
# Build rules
######################################################################

# Object files
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# Default target
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin size

# C files
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

# Assembly files
$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

# Link
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# Binary
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CP) -O binary $< $@

# Print size
size: $(BUILD_DIR)/$(TARGET).elf
	$(SZ) $<

# Create build directory
$(BUILD_DIR):
	mkdir -p $@

######################################################################
# Flash & Clean
######################################################################

reset:
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
		-c "init; reset halt; reset run; exit"

flash: all reset
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

clean:
	rm -rf $(BUILD_DIR)

######################################################################
# Phony
######################################################################

.PHONY: all clean flash reset size
