#
# PBRP product for the Samsung Galaxy C7 (SM-C7000)
#
# AOSP 12.1 (PBRP 4.x) no longer ships embedded.mk/languages_full.mk,
# so inherit whatever exists and always pull in the PBRP common bits.
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product-if-exists, $(SRC_TARGET_DIR)/product/embedded.mk)
$(call inherit-product-if-exists, $(SRC_TARGET_DIR)/product/base.mk)
$(call inherit-product-if-exists, $(SRC_TARGET_DIR)/product/languages_full.mk)

DEVICE_PATH := device/samsung/c7lte
$(call inherit-product, $(DEVICE_PATH)/device.mk)

PRODUCT_DEVICE := c7lte
PRODUCT_NAME := pbrp_c7lte
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-C7000
PRODUCT_MANUFACTURER := samsung

$(call inherit-product, vendor/pb/config/common.mk)
