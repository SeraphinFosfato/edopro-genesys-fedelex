LOCAL_PATH := $(call my-dir)/..
# FASE 119: la scatola compila il gframe del fork (../gframe), non un sottomodulo.
FORK_ROOT := $(LOCAL_PATH)/..

include $(CLEAR_VARS)
LOCAL_MODULE := EDOProClient

LOCAL_CFLAGS := -DYGOPRO_USE_SFML -D_ANDROID -DYGOPRO_BUILD_DLL -Wno-deprecated-declarations -Wextra -Wpedantic -Wall\
				-Wno-unused-parameter -Wno-unused-lambda-capture -Wno-missing-braces -Wno-unused-function\
				-Wno-missing-field-initializers -Wno-inconsistent-missing-override

ifeq ($(APP_OPTIM),debug)
LOCAL_CFLAGS += -g -D_DEBUG
else
LOCAL_CFLAGS += -O3
	ifneq ($(TARGET_ARCH_ABI),armeabi)
		LOCAL_CFLAGS += -flto
		LOCAL_LDFLAGS := -flto
	endif
endif

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_CFLAGS += -mno-unaligned-access
endif
ifeq ($(TARGET_ARCH_ABI), armeabi)
LOCAL_CFLAGS += -mno-unaligned-access
endif

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
LOCAL_LDFLAGS += "-Wl,-z,common-page-size=16384"
endif

ifeq ($(TARGET_ARCH_ABI), x86_64)
LOCAL_LDFLAGS += "-Wl,-z,max-page-size=16384"
LOCAL_LDFLAGS += "-Wl,-z,common-page-size=16384"
endif

ifneq ($(PICS_URL),)
LOCAL_CFLAGS += -DDEFAULT_PIC_URL=\"$(PICS_URL)\"
endif

ifneq ($(FIELDS_URL),)
LOCAL_CFLAGS += -DDEFAULT_FIELD_URL=\"$(FIELDS_URL)\"
endif

ifneq ($(COVERS_URL),)
LOCAL_CFLAGS += -DDEFAULT_COVER_URL=\"$(COVERS_URL)\"
endif

# Stesso default di premake5.lua (newoption update-url); sovrascrivibile da ambiente.
UPDATE_URL ?= https://seraphinfosfato.github.io/Banlist-dist/update.json
ifneq ($(UPDATE_URL),)
LOCAL_CFLAGS += -DUPDATE_URL=\"$(UPDATE_URL)\"
endif

CLASSES_PATH := $(LOCAL_PATH)/deps
EABI_CLASSES_PATH := $(LOCAL_PATH)/deps-armeabi
GFRAME_PATH := $(FORK_ROOT)/gframe
LOCAL_C_INCLUDES := $(GFRAME_PATH)/CGUICustomContextMenu \
					$(GFRAME_PATH)/CGUICustomTabControl \
					$(GFRAME_PATH)/CGUICustomText \
					$(GFRAME_PATH)/CGUIFileSelectListBox \
					$(GFRAME_PATH)/CGUIImageButton \
					$(GFRAME_PATH)/CGUITTFont \
					$(GFRAME_PATH)/CGUIWindowedTabControl \
					$(GFRAME_PATH)/CProgressBar \
					$(GFRAME_PATH)/ResizeablePanel

LOCAL_SRC_FILES := $(GFRAME_PATH)/Android/COSAndroidOperator.cpp \
				   $(GFRAME_PATH)/Android/file_stream.cpp \
				   $(GFRAME_PATH)/Android/porting_android.cpp \
				   $(GFRAME_PATH)/CGUICustomCheckBox/CGUICustomCheckBox.cpp \
				   $(GFRAME_PATH)/CGUICustomContextMenu/CGUICustomContextMenu.cpp \
				   $(GFRAME_PATH)/CGUICustomComboBox/CGUICustomComboBox.cpp \
				   $(GFRAME_PATH)/CGUICustomContextMenu/CGUICustomMenu.cpp \
				   $(GFRAME_PATH)/CGUICustomTabControl/CGUICustomTabControl.cpp \
				   $(GFRAME_PATH)/CGUICustomTable/CGUICustomTable.cpp \
				   $(GFRAME_PATH)/CGUICustomText/CGUICustomText.cpp \
				   $(GFRAME_PATH)/CGUIFileSelectListBox/CGUIFileSelectListBox.cpp \
				   $(GFRAME_PATH)/CGUIImageButton/CGUIImageButton.cpp \
				   $(GFRAME_PATH)/CGUISkinSystem/CConfigMap.cpp \
				   $(GFRAME_PATH)/CGUISkinSystem/CGUIProgressBar.cpp \
				   $(GFRAME_PATH)/CGUISkinSystem/CGUISkinSystem.cpp \
				   $(GFRAME_PATH)/CGUISkinSystem/CImageGUISkin.cpp \
				   $(GFRAME_PATH)/CGUITTFont/CGUITTFont.cpp \
				   $(GFRAME_PATH)/CGUIWindowedTabControl/CGUIWindowedTabControl.cpp \
				   $(GFRAME_PATH)/CProgressBar/CProgressBar.cpp \
				   $(GFRAME_PATH)/CXMLRegistry/CXMLNode.cpp \
				   $(GFRAME_PATH)/CXMLRegistry/CXMLRegistry.cpp \
				   $(GFRAME_PATH)/MD5/md5.c \
				   $(GFRAME_PATH)/ResizeablePanel/ResizeablePanel.cpp \
				   $(GFRAME_PATH)/SoundBackends/sound_threaded_backend.cpp \
				   $(GFRAME_PATH)/SoundBackends/sfml/sound_sfml.cpp \
				   $(GFRAME_PATH)/address.cpp \
				   $(GFRAME_PATH)/client_card.cpp \
				   $(GFRAME_PATH)/client_field.cpp \
				   $(GFRAME_PATH)/client_updater.cpp \
				   $(GFRAME_PATH)/core_utils.cpp \
				   $(GFRAME_PATH)/custom_skin_enum.cpp \
				   $(GFRAME_PATH)/data_manager.cpp \
				   $(GFRAME_PATH)/data_handler.cpp \
				   $(GFRAME_PATH)/deck_con.cpp \
				   $(GFRAME_PATH)/deck_manager.cpp \
				   $(GFRAME_PATH)/discord_wrapper.cpp \
				   $(GFRAME_PATH)/dllinterface.cpp \
				   $(GFRAME_PATH)/drawing.cpp \
				   $(GFRAME_PATH)/duelclient.cpp \
				   $(GFRAME_PATH)/edopro_main.cpp \
				   $(GFRAME_PATH)/event_handler.cpp \
				   $(GFRAME_PATH)/game.cpp \
				   $(GFRAME_PATH)/game_config.cpp \
				   $(GFRAME_PATH)/generic_duel.cpp \
				   $(GFRAME_PATH)/gframe.cpp \
				   $(GFRAME_PATH)/image_downloader.cpp \
				   $(GFRAME_PATH)/image_manager.cpp \
				   $(GFRAME_PATH)/ireadfile_sqlite.cpp \
				   $(GFRAME_PATH)/joystick_wrapper.cpp \
				   $(GFRAME_PATH)/logging.cpp \
				   $(GFRAME_PATH)/materials.cpp \
				   $(GFRAME_PATH)/menu_handler.cpp \
				   $(GFRAME_PATH)/netserver.cpp \
				   $(GFRAME_PATH)/old_replay_mode.cpp \
				   $(GFRAME_PATH)/replay_mode.cpp \
				   $(GFRAME_PATH)/repo_cloner.cpp \
				   $(GFRAME_PATH)/repo_manager.cpp \
				   $(GFRAME_PATH)/replay.cpp \
				   $(GFRAME_PATH)/server_lobby.cpp \
				   $(GFRAME_PATH)/settings_window.cpp \
				   $(GFRAME_PATH)/single_mode.cpp \
				   $(GFRAME_PATH)/sound_manager.cpp \
				   $(GFRAME_PATH)/utils.cpp \
				   $(GFRAME_PATH)/utils_gui.cpp \
				   $(GFRAME_PATH)/windbot.cpp \
				   $(GFRAME_PATH)/windbot_panel.cpp \
				   $(GFRAME_PATH)/banlist_diff.cpp \
				   $(GFRAME_PATH)/banlist_diff_format.cpp \
				   $(GFRAME_PATH)/banlist_updater.cpp \
				   $(GFRAME_PATH)/banlist_verify.cpp \
				   $(GFRAME_PATH)/cloud_decks.cpp \
				   $(GFRAME_PATH)/deep_link.cpp \
				   $(GFRAME_PATH)/launcher_logic.cpp \
				   $(GFRAME_PATH)/player_panel.cpp \
				   $(GFRAME_PATH)/replay_oscura.cpp \
				   $(GFRAME_PATH)/server_tls.cpp \
				   $(GFRAME_PATH)/sha256.cpp \
				   $(GFRAME_PATH)/title_checkin.cpp \
				   $(GFRAME_PATH)/title_state.cpp \
				   $(GFRAME_PATH)/title_store.cpp \
				   $(GFRAME_PATH)/title_verify.cpp \
				   $(GFRAME_PATH)/tls_client.cpp \
				   $(GFRAME_PATH)/tls_roots.cpp \
				   $(GFRAME_PATH)/tournament_mode.cpp \
				   $(GFRAME_PATH)/update_verify.cpp \
				   $(GFRAME_PATH)/tweetnacl/randombytes.c \
				   $(GFRAME_PATH)/tweetnacl/tweetnacl.c

ifneq ($(USE_BUNDLED_FONT),)
LOCAL_SRC_FILES += $(GFRAME_PATH)/CGUITTFont/bundled_font.cpp
LOCAL_CFLAGS += -DYGOPRO_USE_BUNDLED_FONT
endif

LOCAL_STATIC_LIBRARIES := Irrlicht \
						  libevent2 \
						  clzma \
						  sqlite3 \
						  libft2 \
						  fmt_static \
						  libssl \
						  git2 \
						  curl \
						  sfAudio \
						  nlohmann-json

LOCAL_SHARED_LIBRARIES := ocgcore

include $(BUILD_SHARED_LIBRARY)

$(call import-add-path,$(CLASSES_PATH))
$(call import-add-path,$(FORK_ROOT))
$(call import-module,irrlicht)
$(call import-module,libevent)
$(call import-module,sqlite3)
$(call import-module,freetype)
$(call import-module,gframe/lzma)
$(call import-module,ocgcore)
$(call import-module,fmt)
$(call import-module,openssl)
$(call import-module,libgit2)
$(call import-module,curl)
$(call import-module,sfAudio)
$(call import-module,nlohmann-json)

include $(CLASSES_PATH)/copy-assets/Android.mk
