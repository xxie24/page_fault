LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := fault

LOCAL_SRC_FILES += fault.c tlb_thrash.c

#LOCAL_ARM_MODE := arm

include $(BUILD_EXECUTABLE)
