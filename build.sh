#!/bin/sh

if [ -e /etc/debian_version ]; then
    PACKAGEFORMAT=deb
elif [ -e /etc/redhat-release ]; then
    PACKAGEFORMAT=rpm
fi

./autogen.sh --with-help --disable-firebird-sdbc --with-system-postgresql --with-lang="zh-CN" --disable-librelogo --disable-extensions --with-package-format=${PACKAGEFORMAT} --enable-epm --enable-release-build --enable-odk && make

if [ -d ./out ]; then
    rm -rf ./out && mkdir out
fi

if [ "$PACKAGEFORMAT" = rpm ]; then
    cp ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_7.3.6.2.0_Linux_rpm_langpack_zh-CN/RPMS/loongofficebasis-zh-CN-help-7.3.6.2.0-2.loongarch64.rpm \
       ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_7.3.6.2.0_Linux_rpm_langpack_zh-CN/RPMS/loongoffice-zh-CN-7.3.6.2.0-2.loongarch64.rpm \
       ./workdir/installation/LoongOffice_languagepack/rpm/install/LoongOffice_7.3.6.2.0_Linux_rpm_langpack_zh-CN/RPMS/loongofficebasis-zh-CN-7.3.6.2.0-2.loongarch64.rpm \
       ./workdir/installation/LoongOffice/rpm/install/LoongOffice_7.3.6.2.0_Linux_rpm/RPMS/loongoffice-7.3.6.2.0-2.loongarch64.rpm \
       ./workdir/installation/LoongOffice/rpm/install/LoongOffice_7.3.6.2.0_Linux_rpm/RPMS/loongoffice-freedesktop-menus-7.3.6-2.noarch.rpm \
       ./out
    exit 0
fi

cd out

MACHINE=`uname -m`
if [ "$MACHINE" = mips64 ]; then
    MACHINE=mips64el
fi

cp ../workdir/installation/LoongOffice/deb/install/LoongOffice_7.3.6.2.0_Linux_deb/DEBS/loongoffice_7.3.6.2.0-2_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_7.3.6.2.0_Linux_deb/DEBS/loongoffice-debian-menus_7.3.6-2_all.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn_7.3.6.2.0-2_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn-help_7.3.6.2.0-2_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongoffice-zh-cn_7.3.6.2.0-2_${MACHINE}.deb \
   ../workdir/installation/LoongOffice_SDK/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_sdk/DEBS/loongoffice-sdk_7.3.6.2.0-2_${MACHINE}.deb .

if [ -d ./extract ]; then
    rm -rf ./extract
fi

if [ -d ./extract-sdk ]; then
    rm -rf ./extract-sdk
fi

mkdir -p ./extract/DEBIAN
mkdir -p ./extract-sdk/DEBIAN

dpkg-deb -x ./loongoffice-zh-cn_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn-help_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-debian-menus_7.3.6-2_all.deb extract
dpkg-deb -x ./loongoffice_7.3.6.2.0-2_${MACHINE}.deb extract

dpkg-deb -e ./loongoffice-debian-menus_7.3.6-2_all.deb extract/DEBIAN
dpkg-deb -e ./loongoffice_7.3.6.2.0-2_${MACHINE}.deb extract/DEBIAN

dpkg-deb -x ./loongoffice-sdk_7.3.6.2.0-2_${MACHINE}.deb extract-sdk
dpkg-deb -e ./loongoffice-sdk_7.3.6.2.0-2_${MACHINE}.deb extract-sdk/DEBIAN

sed -i "s/7.3.6.2.0-2$/7.3.6.2.0-2.lnd.1.0.0006/g" extract/DEBIAN/control
sed -i "s/.*Installed-Size.*/Installed-Size:\ `du -sk extract | awk '{print $1}'`/g" extract/DEBIAN/control

if [ ! -d ./build ]; then
    mkdir ./build
fi

dpkg-deb -b extract build && cp -a build/*.deb .

cd ..

exit 0
