LOCAL_PATH := device/samsung/c7lte

# Extra TWRP flags file; recovery.fstab itself is installed by the build
# system through TARGET_RECOVERY_FSTAB.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/twrp.flags:$(TARGET_COPY_OUT_RECOVERY)/root/system/etc/twrp.flags
