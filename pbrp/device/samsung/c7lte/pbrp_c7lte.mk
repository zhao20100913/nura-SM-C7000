#
# PBRP product for the Samsung Galaxy C7 (SM-C7000)
#
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/embedded.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)

DEVICE_PATH := device/samsung/c7lte
$(call inherit-product, $(DEVICE_PATH)/device.mk)

PRODUCT_DEVICE := c7lte
PRODUCT_NAME := pbrp_c7lte
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-C7000
PRODUCT_MANUFACTURER := samsung

PRODUCT_GMS_CLIENTID_BASE := android-samsung

$(call inherit-product, vendor/pb/config/common.mk)
