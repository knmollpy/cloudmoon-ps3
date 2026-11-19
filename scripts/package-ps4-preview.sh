#!/usr/bin/env bash
set -euo pipefail

: "${OO_PS4_TOOLCHAIN:?OO_PS4_TOOLCHAIN must point to OpenOrbis PS4Toolchain}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="${OO_PS4_TOOLCHAIN}/bin/linux"
BUILD_DIR="${ROOT}/build-ps4-modules"
OUT_DIR="${ROOT}/dist/cloudmoon-ps4"
TITLE_ID="CLDM00001"
CONTENT_ID="IV0000-CLDM00001_00-CLOUDMOON0000001"
APP_ELF="${BUILD_DIR}/cloudmoon_ps4_app.elf"

test -x "${TOOLS}/create-fself"
test -x "${TOOLS}/create-gp4"
test -f "${TOOLS}/PkgTool.Core"
test -f "${APP_ELF}"
test -s "${ROOT}/assets/cacert.pem"

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/sce_sys" "${OUT_DIR}/assets/misc"
cp "${ROOT}/assets/cacert.pem" "${OUT_DIR}/assets/misc/cacert.pem"
cp "${ROOT}/scripts/generate-ps4-icon.py" "${OUT_DIR}/generate-icon.py"
(
    cd "${OUT_DIR}"
    python3 generate-icon.py
    rm generate-icon.py
)

(
    cd "${OUT_DIR}"
    "${TOOLS}/create-fself" \
        -in="${APP_ELF}" \
        -out="cloudmoon_ps4_app.oelf" \
        --eboot "eboot.bin" \
        --paid 0x3800000000000011

    "${TOOLS}/PkgTool.Core" sfo_new sce_sys/param.sfo
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value '0.01'
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value 'gd'
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "${CONTENT_ID}"
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value 'CloudMoon PS4'
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "${TITLE_ID}"
    "${TOOLS}/PkgTool.Core" sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value '0.01'

    "${TOOLS}/create-gp4" -out cloudmoon_ps4.gp4 \
        --content-id="${CONTENT_ID}" \
        --files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png assets/misc/cacert.pem"
    "${TOOLS}/PkgTool.Core" pkg_build cloudmoon_ps4_preview.gp4 .
)

PKG_PATH="$(find "${OUT_DIR}" -maxdepth 2 -type f -name '*.pkg' -print -quit)"
test -n "${PKG_PATH}"
test -s "${PKG_PATH}"
echo "CloudMoon PS4 application PKG created: ${PKG_PATH}"
