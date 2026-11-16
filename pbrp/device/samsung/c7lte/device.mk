LOCAL_PATH := device/samsung/c7lte

# Recovery ramdisk files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/recovery.fstab:$(TARGET_COPY_OUT_RECOVERY)/root/system/etc/recovery.fstab
