#
# Copyright (C) 2024 Hajo Noerenberg <hajo-linkplay-emu@noerenberg.de>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=linkplay-emu
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/linkplay-emu
  SECTION:=sound
  CATEGORY:=Sound
  TITLE:=Linkplay sound control daemon
  MAINTAINER:=Hajo Noerenberg <hajo-linkplay-emu@noerenberg.de>
  DEPENDS:=@AUDIO_SUPPORT +alsa-lib
  URL:=https://github.com/hn/linkplay-a31
endef

define Package/linkplay-emu/description
 Daemon for emulating Linkplay WiFi module sound control.
endef

TARGET_LDFLAGS += -lasound

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Wall" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/linkplay-emu/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/linkplay-emu $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/linkplay-emu.init $(1)/etc/init.d/linkplay-emu
endef

$(eval $(call BuildPackage,linkplay-emu))
