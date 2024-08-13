LOCAL_PATH:= $(call my-dir)

AML_MP_PLAYER_DEMO_SRCS := \
	AmlMpPlayerDemo.cpp \

AML_MP_PLAYER_DEMO_INC :=

AML_MP_PLAYER_DEMO_CFLAGS := -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION) \
	-Werror -Wsign-compare  -Wno-multichar

AML_MP_PLAYER_DEMO_SHARED_LIBS := \
	libutils \
	libcutils \
	liblog \
	libnativewindow \
	libui \
	libstagefright_foundation \

AML_MP_PLAYER_DEMO_SHARED_LIBS_28 := \
	libamdvr.product \
	libaml_mp_sdk \
	libgui \

AML_MP_PLAYER_DEMO_SHARED_LIBS_29 := \
	libamdvr.product \
	libaml_mp_sdk \
	libgui \

AML_MP_PLAYER_DEMO_SHARED_LIBS_ge_30 := \
	libamdvr.system \
    libaml_mp_sdk \
	libgui \

AML_MP_PLAYER_DEMO_VENDOR_SHARED_LIBS_28 := \
	libamdvr \
	libaml_mp_sdk.vendor \

AML_MP_PLAYER_DEMO_VENDOR_SHARED_LIBS_29 := \
	libamdvr \
    libaml_mp_sdk.vendor \

AML_MP_PLAYER_DEMO_VENDOR_SHARED_LIBS_ge_30 := \
	libamdvr \
    libaml_mp_sdk.vendor \

AML_MP_PLAYER_DEMO_STATIC_LIBS := \
	libamlMpTestSupporter

AML_MP_PLAYER_DEMO_VENDOR_STATIC_LIBS := \
	libamlMpTestSupporter.vendor

###############################################################################
include $(CLEAR_VARS)
LOCAL_MODULE := amlMpPlayerDemo
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-MIT legacy_by_exception_only legacy_notice
LOCAL_LICENSE_CONDITIONS := by_exception_only notice restricted
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/../../LICENSE
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(AML_MP_PLAYER_DEMO_SRCS)
LOCAL_CFLAGS := $(AML_MP_PLAYER_DEMO_CFLAGS)
LOCAL_C_INCLUDES := $(AML_MP_PLAYER_DEMO_INC)
LOCAL_SHARED_LIBRARIES := $(AML_MP_PLAYER_DEMO_SHARED_LIBS) $(AML_MP_PLAYER_DEMO_SHARED_LIBS_$(PLATFORM_SDK_VERSION))
ifeq (1, $(shell expr $(PLATFORM_SDK_VERSION) \>= 30))
LOCAL_SHARED_LIBRARIES += $(AML_MP_PLAYER_DEMO_SHARED_LIBS_ge_30)
endif
LOCAL_STATIC_LIBRARIES := $(AML_MP_PLAYER_DEMO_STATIC_LIBS)
#LOCAL_WHOLE_STATIC_LIBRARIES :=
#LOCAL_LDFLAGS :=
ifeq (1, $(shell expr $(PLATFORM_SDK_VERSION) \>= 30))
LOCAL_SYSTEM_EXT_MODULE := true
endif
include $(BUILD_EXECUTABLE)

###############################################################################
include $(CLEAR_VARS)
LOCAL_MODULE := amlMpPlayerDemo.vendor
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-MIT legacy_by_exception_only legacy_notice
LOCAL_LICENSE_CONDITIONS := by_exception_only notice restricted
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/../../LICENSE
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(AML_MP_PLAYER_DEMO_SRCS)
LOCAL_CFLAGS := $(AML_MP_PLAYER_DEMO_CFLAGS)
LOCAL_C_INCLUDES := $(AML_MP_PLAYER_DEMO_INC)
LOCAL_SHARED_LIBRARIES := $(AML_MP_PLAYER_DEMO_SHARED_LIBS) $(AML_MP_PLAYER_DEMO_VENDOR_SHARED_LIBS_$(PLATFORM_SDK_VERSION))
ifeq (1, $(shell expr $(PLATFORM_SDK_VERSION) \>= 30))
LOCAL_SHARED_LIBRARIES += $(AML_MP_PLAYER_DEMO_VENDOR_SHARED_LIBS_ge_30)
endif
LOCAL_STATIC_LIBRARIES := $(AML_MP_PLAYER_DEMO_VENDOR_STATIC_LIBS)
#LOCAL_WHOLE_STATIC_LIBRARIES :=
#LOCAL_LDFLAGS :=
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
