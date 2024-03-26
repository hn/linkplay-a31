#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if [ ! -d ./target/linux/ramips/dts ]; then
	echo "Change to OpenWrt source dir!" >&2
	exit 1
fi

echo "Adding Linkplay A31 SoM support to OpenWrt"

# Kernel support
cp -v -n $SCRIPT_DIR/mt7628an_linkplay_a31.dts ./target/linux/ramips/dts/
patch -p1 < $SCRIPT_DIR/mt7628an_linkplay_a31-mk.diff
cp -v $SCRIPT_DIR/defconfig .config

# I2S crash workaround
cp -v -n $SCRIPT_DIR/836-mt7688-i2s-audio-crash-workaround.patch ./target/linux/ramips/patches-5*/

# Linkplay sound control daemon
cp -v -r -n $SCRIPT_DIR/linkplay-emu ./package/

# Shairport-sync mixer name
patch -p1 < $SCRIPT_DIR/shairport-sync-config.diff

# System config
cp -v -r -n $SCRIPT_DIR/files/ ./

