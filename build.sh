#!/bin/sh
set -e
if [ -e /etc/debian_version ]; then
    PACKAGEFORMAT=deb
elif [ -e /etc/loongnix_version ]; then
    PACKAGEFORMAT=deb
elif [ -e /etc/redhat-release ]; then
    PACKAGEFORMAT=rpm
fi

echo "== apply translations patch =="

# 获取脚本所在目录的绝对路径（build 目录）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$(dirname "$SCRIPT_DIR")"  # 25.8

PATCH_FILE="${BUILD_DIR}/loongoffice/translations/rehearse-timings-zh-CN.patch"
TRANS_DIR="${BUILD_DIR}/translations"

if [ -f "$PATCH_FILE" ]; then
    cd "$TRANS_DIR"

    # 防止重复应用
    if git apply --check "$PATCH_FILE" 2>/dev/null; then
        git apply "$PATCH_FILE"
        echo "patch applied"
    else
        echo "patch already applied or conflict"
    fi

    cd "$SCRIPT_DIR"  # 回到 build 目录
fi

./autogen.sh --with-help --disable-firebird-sdbc --with-system-postgresql --with-lang="zh-CN" --disable-librelogo --with-package-format=${PACKAGEFORMAT} --enable-epm --enable-release-build --enable-odk --with-vendor=Loongson --enable-qt6-multimedia --enable-qt6 --enable-kf6 --enable-gstreamer-1-0 && make

if [ -d ./out ]; then
    rm -rf ./out
fi

mkdir out

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
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-extension-pdf-import_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-xsltfilter_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongofficebasis-kde-integration_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-pyuno_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-graphicfilter_25.8.7.0.0-1_${MACHINE}.deb \
   ../workdir/installation/LoongOffice/deb/install/LoongOffice_25.8.7.0.0_Linux_deb/DEBS/loongoffice-ogltrans_25.8.7.0.0-1_${MACHINE}.deb \
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
dpkg-deb -x ./loongoffice-extension-pdf-import_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-xsltfilter_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongofficebasis-kde-integration_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-pyuno_25.8.7.0.0-1_${MACHINE}.deb extract
dpkg-deb -x ./loongoffice-graphicfilter_25.8.7.0.0-1_${MACHINE}.deb extract
#dpkg-deb -x ./loongoffice-ogltrans_25.8.7.0.0-1_${MACHINE}.deb extract

dpkg-deb -e ./loongoffice-debian-menus_25.8.7-0_all.deb extract/DEBIAN
dpkg-deb -e ./loongoffice_25.8.7.0.0-1_${MACHINE}.deb extract/DEBIAN

dpkg-deb -x ./loongoffice-sdk_25.8.7.0.0-1_${MACHINE}.deb extract-sdk
dpkg-deb -e ./loongoffice-sdk_25.8.7.0.0-1_${MACHINE}.deb extract-sdk/DEBIAN

sed -i "s/25.8.7.0.0-1$/25.8.7.0.0-1.lnd.1.0.0001/g" extract/DEBIAN/control
sed -i "s/.*Installed-Size.*/Installed-Size:\ `du -sk extract | awk '{print $1}'`/g" extract/DEBIAN/control
echo "Replaces: loongoffice-zh-cn,loongofficebasis-zh-cn-help,loongofficebasis-zh-cn,loongoffice-debian-menus" >> extract/DEBIAN/control
echo "Conflicts: loongoffice-zh-cn,loongofficebasis-zh-cn-help,loongofficebasis-zh-cn,loongoffice-debian-menus" >> extract/DEBIAN/control
echo "Section: editors" >> extract/DEBIAN/control
echo "Priority: optional" >> extract/DEBIAN/control

# 安装 OFD 阅读器扩展（使用 bundled 方式）
mkdir -p extract/opt/loongoffice/share/extensions/ofdreader/
OFD_DIR="extract/opt/loongoffice/share/extensions/ofdreader"

# 下载扩展文件（如果不存在或为空）
if [ ! -f "$OFD_DIR/loongoffice-ofd-extension.oxt" ] || [ ! -s "$OFD_DIR/loongoffice-ofd-extension.oxt" ]; then
    echo "正在下载 OFD 扩展..."
    wget -O "$OFD_DIR/loongoffice-ofd-extension.oxt" \
        "https://github.com/fanta0625/libreoffice-ofd-extension/releases/download/v1.0.0/loongoffice-ofd-extension.oxt"
fi

# 解压到目录
cd "$OFD_DIR"
unzip -o loongoffice-ofd-extension.oxt 2>/dev/null
rm -f loongoffice-ofd-extension.oxt
cd -

# 设置权限
find extract/opt/loongoffice/share/extensions/ofdreader/ -type f -exec chmod 644 {} \;
find extract/opt/loongoffice/share/extensions/ofdreader/ -type d -exec chmod 755 {} \;

echo "OFD 扩展安装脚本准备完成"

if [ ! -d ./build ]; then
    mkdir ./build
fi

dpkg-deb -b extract build && cp -a build/*.deb .

cd ..

exit 0
