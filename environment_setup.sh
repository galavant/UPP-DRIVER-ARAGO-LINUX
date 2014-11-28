export KERNEL_DIR=/home/am1x_sdk/board-support/linux-2.6.37-psp03.21.00.04.sdk/
export SDK_PATH=/home/am1x_sdk/linux-devkit/
export TOOLCHAIN_PATH=/home/aniq/setups/arago-2011.09-armv5te-linux-gnueabi-sdk/arago-2011.09/armv5te/

## export KERNEL_DIR=/home/hassan/Applications/omapL138Sdk/psp/linux-2.6.37-psp03.21.00.04.sdk/
## export SDK_PATH=/home/hassan/Applications/omapL138Sdk/linux-devkit/
## export TOOLCHAIN_PATH=/home/hassan/Applications/codeSourceryToolchain/arm-2009q1/

export TARGET_SYS=arm-arago-linux-gnueabi
export COMPILER_PREFIX=arm-arago-linux-gnueabi-
export PATH=$SDK_PATH/bin:$TOOLCHAIN_PATH/bin:$PATH
export CPATH=$SDK_PATH/$TARGET_SYS/usr/include:$CPATH
#export LIBTOOL_SYSROOT_PATH=$SDK_PATH/$TARGET_SYS
#export PKG_CONFIG_SYSROOT_DIR=$SDK_PATH/$TARGET_SYS
#export PKG_CONFIG_PATH=$SDK_PATH/$TARGET_SYS/usr/lib/pkgconfig
#export CONFIG_SITE=$SDK_PATH/site-config
#alias opkg="LD_LIBRARY_PATH=$SDK_PATH/lib $SDK_PATH/bin/opkg-cl -f $SDK_PATH/etc/opkg-sdk.conf -o $SDK_PATH"
#alias opkg-target="LD_LIBRARY_PATH=$SDK_PATH/lib $SDK_PATH/bin/opkg-cl -f $SDK_PATH/$TARGET_SYS/etc/opkg.conf -o $SDK_PATH/$TARGET_SYS"
export CC=arm-arago-linux-gnueabi-gcc
export CPP="arm-none-linux-gnueabi-gcc -E"
export NM=arm-arago-linux-gnueabi-nm
export RANLIB=arm-arago-linux-gnueabi-ranlib
export OBJCOPY=arm-arago-linux-gnueabi-objcopy
export STRIP=arm-arago-linux-gnueabi-strip
export AS=arm-arago-linux-gnueabi-as
export AR=arm-arago-linux-gnueabi-ar
export OBJDUMP=arm-arago-linux-gnueabi-objdump
export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
#export OE_QMAKE_CC=${TARGET_SYS}-gcc
#export OE_QMAKE_CXX=${TARGET_SYS}-g++
#export OE_QMAKE_LINK=${TARGET_SYS}-g++
#export OE_QMAKE_AR=${TARGET_SYS}-ar
#export OE_QMAKE_LIBDIR_QT=${SDK_PATH}/${TARGET_SYS}/usr/lib
#export OE_QMAKE_INCDIR_QT=${SDK_PATH}/${TARGET_SYS}/usr/include/qtopia
#export OE_QMAKE_MOC=${SDK_PATH}/bin/moc4
#export OE_QMAKE_UIC=${SDK_PATH}/bin/uic4
#export OE_QMAKE_UIC3=${SDK_PATH}/bin/uic34
#export OE_QMAKE_RCC=${SDK_PATH}/bin/rcc4
#export OE_QMAKE_QDBUSCPP2XML=${SDK_PATH}/bin/qdbuscpp2xml4
#export OE_QMAKE_QDBUSXML2CPP=${SDK_PATH}/bin/qdbusxml2cpp4
#export OE_QMAKE_QT_CONFIG=${SDK_PATH}/${TARGET_SYS}/usr/share/qtopia/mkspecs/qconfig.pri
#export QMAKESPEC=${SDK_PATH}/${TARGET_SYS}/usr/share/qtopia/mkspecs/linux-g++
#export OE_QMAKE_LDFLAGS="-L${SDK_PATH}/${TARGET_SYS}/usr/lib -Wl,-rpath-link,${SDK_PATH}/${TARGET_SYS}/usr/lib -Wl,-O1 -Wl,--hash-style=gnu"
#export OE_QMAKE_STRIP="echo"
export PS1="\[\e[32;1m\][upp-make-environment]\[\e[0m\]:\w> "
