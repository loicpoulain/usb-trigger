LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := src/usb-trigger.c

LOCAL_MODULE := usb_trigger

include $(BUILD_EXECUTABLE)
