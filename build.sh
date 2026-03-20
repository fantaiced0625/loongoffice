#!/bin/sh

if [ -e /etc/debian_version ]; then
    PACKAGEFORMAT=deb
elif [ -e /etc/redhat-release ]; then
    PACKAGEFORMAT=rpm
fi

./autogen.sh --with-help --disable-firebird-sdbc --with-system-postgresql --with-lang="zh-CN" --disable-librelogo --with-package-format=${PACKAGEFORMAT} --enable-epm --enable-release-build --enable-odk --with-vendor=Loongson --enable-qt6-multimedia --enable-qt6 --enable-kf6 --enable-gstreamer-1-0 --enable-sal-log --enable-symbols --enable-optimized=debug --with-java && make

if [ -d ./out ]; then
    rm -rf ./out && mkdir out
fi

if [ "$PACKAGEFORMAT" = rpm ]; then
    cp ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_25.8.7.0.0_Linux_rpm_langpack_zh-CN/RPMS/loongofficebasis-zh-CN-help-25.8.7.0.0-1.loongarch64.rpm \
       ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_25.8.7.0.0_Linux_rpm_langpack_zh-CN/RPMS/loongoffice-zh-CN-25.8.7.0.0-1.loongarch64.rpm \
       ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_25.8.7.0.0_Linux_rpm_langpack_zh-CN/RPMS/loongofficebasis-zh-CN-25.8.7.0.0-1.loongarch64.rpm \
       ./workdir/installation/LoongOffice/rpm/install/LoongOffice_25.8.7.0.0_Linux_rpm/RPMS/loongoffice-25.8.7.0.0-1.loongarch64.rpm \
       ./workdir/installation/LoongOffice/rpm/install/LoongOffice_25.8.7.0.0_Linux_rpm/RPMS/loongoffice-freedesktop-menus-25.8.7-0.noarch.rpm \
       ./out
    exit 0
fi

cd out

MACHINE=`uname -m`
if [ "$MACHINE" = mips64 ]; then
    MACHINE=mips64el
fi

cp ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-debian-menus_25.8.7-0_all.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_25.8.7.0.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_25.8.7.0.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn-help_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_25.8.7.0.0_Linux_deb_langpack_zh-CN/DEBS/loongoffice-zh-cn_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_SDK/deb/install/LoongOffice_25.8.7.0.0_Linux_deb_sdk/DEBS/loongoffice-sdk_25.8.7.0.0-1_${MACHINE}.deb .

if [ -d ./extract ]; then
    rm -rf ./extract
fi

if [ -d ./extract-sdk ]; then
    rm -rf ./extract-sdk
fi

mkdir -p ./extract/DEBIAN
mkdir -p ./extract-sdk/DEBIAN

dpkg-deb -x ./loongoffice-zh-cn_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn-help_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-debian-menus_25.8.7-0_all.deb extract
dpkg-deb -x ./loongoffice_25.8.7.0.0-1_${MACHINE}.deb extract

dpkg-deb -e ./loongoffice-debian-menus_25.8.7-0_all.deb extract/DEBIAN
dpkg-deb -e ./loongoffice_25.8.7.0.0-1_${MACHINE}.deb extract/DEBIAN

dpkg-deb -x ./loongoffice-sdk_25.8.7.0.0-1_${MACHINE}.deb extract-sdk
dpkg-deb -e ./loongoffice-sdk_25.8.7.0.0-1_${MACHINE}.deb extract-sdk/DEBIAN

sed -i "s/25.8.7.0.0-1$/25.8.7.0.0-1.lnd.1.0.0001/g" extract/DEBIAN/control
sed -i "s/.*Installed-Size.*/Installed-Size:\ `du -sk extract | awk '{print $1}'`/g" extract/DEBIAN/control

# 安装 OFD 阅读器扩展（下载 oxt 文件）
mkdir -p extract/usr/lib/loongoffice/share/extensions/ofdreader/
wget -q -O extract/usr/lib/loongoffice/share/extensions/ofdreader/loongoffice-ofd-extension.oxt \
    "https://github.com/fanta0625/libreoffice-ofd-extension/releases/download/v1.0.0/libreoffice-ofd-extension.oxt"

# 在 postinst 中添加插件安装命令
if [ -f extract/DEBIAN/postinst ]; then
    echo "/opt/loongoffice/program/unopkg add /usr/lib/loongoffice/share/extensions/ofdreader/loongoffice-ofd-extension.oxt" >> extract/DEBIAN/postinst
fi

if [ ! -d ./build ]; then
    mkdir ./build
fi

dpkg-deb -b extract build && cp -a build/*.deb .

cd ..

exit 0
