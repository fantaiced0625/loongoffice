#!/bin/sh

if [ -e /etc/debian_version ]; then
    PACKAGEFORMAT=deb
elif [ -e /etc/redhat-release ]; then
    PACKAGEFORMAT=rpm
fi

./autogen.sh --with-help --disable-firebird-sdbc --with-system-postgresql --with-lang="zh-CN" --disable-librelogo --disable-extensions --with-package-format=${PACKAGEFORMAT} --enable-epm --enable-release-build && make

if [ "$PACKAGEFORMAT" = rpm ]; then
    exit 0
fi

if [ -d ./out ]; then
    rm -rf ./out
fi
mkdir out && cd out

MACHINE=`uname -m`
if [ "$MACHINE" = mips64 ]; then
    MACHINE=mips64el
fi

cp ../workdir/installation/LoongOffice/deb/install/LoongOffice_7.3.6.2.0_Linux_deb/DEBS/loongoffice_7.3.6.2.0-2_${MACHINE}.deb .
cp ../workdir/installation/LoongOffice/deb/install/LoongOffice_7.3.6.2.0_Linux_deb/DEBS/loongoffice-debian-menus_7.3.6-2_all.deb .
cp ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn_7.3.6.2.0-2_${MACHINE}.deb .
cp ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongofficebasis-zh-cn-help_7.3.6.2.0-2_${MACHINE}.deb .
cp ../workdir/installation/LoongOffice_languagepack/deb/install/LoongOffice_7.3.6.2.0_Linux_deb_langpack_zh-CN/DEBS/loongoffice-zh-cn_7.3.6.2.0-2_${MACHINE}.deb .

if [ -d ./extract ]; then
    rm -rf ./extract
fi

mkdir -p ./extract/DEBIAN

dpkg-deb -x ./loongoffice-zh-cn_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn-help_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-zh-cn_7.3.6.2.0-2_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-debian-menus_7.3.6-2_all.deb extract
dpkg-deb -x ./loongoffice_7.3.6.2.0-2_${MACHINE}.deb extract

dpkg-deb -e ./loongoffice-debian-menus_7.3.6-2_all.deb extract/DEBIAN
dpkg-deb -e ./loongoffice_7.3.6.2.0-2_${MACHINE}.deb extract/DEBIAN

sed -i "s/7.3.6.2.0-2$/7.3.6.2.0-2.lnd.1.0.0005/g" extract/DEBIAN/control
sed -i "s/.*Installed-Size.*/Installed-Size:\ `du -sk extract | awk '{print $1}'`/g" extract/DEBIAN/control

if [ ! -d ./build ]; then
    mkdir ./build
fi

dpkg-deb -b extract build

cd ..

exit 0
