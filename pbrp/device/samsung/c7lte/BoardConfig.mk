#
# PBRP / TWRP board configuration for the Samsung Galaxy C7 (SM-C7000)
#
# Platform: Qualcomm MSM8953 (Snapdragon 625), arm64 + 32-bit compat
# Stock:    C7000ZCS3CRJ1 (Android 8.0.0, non-Treble, FDE)
#
# The kernel is prebuilt: the stock boot image ships an "Image.gz-dtb"
# (gzip-compressed Image with the QCDT device trees appended inside the
# kernel blob), so the recovery image must be assembled the same way.
#

DEVICE_PATH := device/samsung/c7lte

# Platform
TARGET_BOARD_PLATFORM := msm8953
TARGET_BOARD_PLATFORM_GPU := qcom-adreno506
TARGET_BOOTLOADER_BOARD_NAME := MSM8953
TARGET_NO_BOOTLOADER := true

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := cortex-a53
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a53

# Kernel: prebuilt stock Image.gz-dtb (dtb is appended to the kernel)
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/Image.gz-dtb
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_RAMDISK_OFFSET := 0x02000000
BOARD_KERNEL_TAGS_OFFSET := 0x01e00000
BOARD_KERNEL_CMDLINE := console=ttyMSM0,115200n8 androidboot.hardware=qcom msm_rtb.filter=0x237 ehci-hcd.park=3 lpm_levels.sleep_disabled=1 androidboot.selinux=permissive
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset $(BOARD_RAMDISK_OFFSET) --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) --kernel_offset $(BOARD_KERNEL_OFFSET)

# Partitions (stock PIT)
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3674210304
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11374931968
BOARD_CACHEIMAGE_PARTITION_SIZE := 314572800
BOARD_FLASH_BLOCK_SIZE := 131072

# Filesystems
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
TARGET_RECOVERY_DEVICE_MODULES :=

# TWRP/PBRP flags
TW_THEME := portrait_hdpi
TW_EXTRA_LANGUAGES := true
TW_SCREEN_BLANK_ON_BOOT := true
TW_USE_TOOLBOX := true
TW_EXCLUDE_TWRPAPP := true
TW_INCLUDE_NTFS_3G := true
TW_INCLUDE_FUSE_EXFAT := true
TW_MAX_BRIGHTNESS := 255
TW_DEFAULT_BRIGHTNESS := 155
TW_BRIGHTNESS_PATH := "/sys/class/leds/lcd-backlight/brightness"
TW_INPUT_BLACKLIST := "hbtp_vm"
TW_EXCLUDE_SUPERSU := true
TW_DEVICE_VERSION := Nura-C7
TW_NO_LEGACY_PROPS := true
TW_INCLUDE_CRYPTO := false
TW_HAS_NO_RECOVERY_PARTITION := false

# Misc
TARGET_SYSTEM_PROP := $(DEVICE_PATH)/system.prop
TARGET_OTA_ASSERT_DEVICE := c7lte,c7ltechn,SM-C7000,C7000
