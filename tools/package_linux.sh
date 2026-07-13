#!/bin/bash
#==========================================================================
# PulseQt Linux 打包脚本 — 生成 AppImage
#
# 用法:
#   bash tools/package_linux.sh              # Release → AppImage
#   bash tools/package_linux.sh --debug       # Debug 构建 + 打包
#   bash tools/package_linux.sh --no-appimage # 只打包目录，不生成 AppImage
#
# 前提:
#   sudo apt install cmake build-essential qt6-base-dev \
#       libqt6serialport6-dev libqt6sql6-sqlite libgl1-mesa-dev fuse3
#==========================================================================

set -e

BUILD_TYPE="Release"
DO_APPIMAGE=true
APPDIR="PulseQt.AppDir"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)   BUILD_TYPE="Debug"; shift ;;
        --no-appimage) DO_APPIMAGE=false; shift ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "=== PulseQt Linux 打包 ($BUILD_TYPE) ==="

# ── 1. 构建 ──────────────────────────────────────────
echo "[1/5] Building..."
cmake -B build-rel -DCMAKE_BUILD_TYPE="$BUILD_TYPE" 2>&1 | tail -2
cmake --build build-rel -j"$(nproc)" 2>&1 | tail -5

# ── 2. 创建 AppDir 结构 ─────────────────────────────
echo "[2/5] Creating AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications"

cp build-rel/PulseQt "$APPDIR/usr/bin/"
cp tools/tcp_wave_simulator.py "$APPDIR/usr/bin/"
cp tools/serial_wave_simulator.py "$APPDIR/usr/bin/"
cp tools/tcp_data_simulator.py "$APPDIR/usr/bin/"

# 桌面入口
cat > "$APPDIR/usr/share/applications/PulseQt.desktop" << 'EOF'
[Desktop Entry]
Name=PulseQt
Comment=Multi-channel Data Acquisition
Exec=PulseQt
Type=Application
Categories=Development;
EOF

# 图标（空占位 — 后续替换为实际 png）
cp "$APPDIR/usr/share/applications/PulseQt.desktop" "$APPDIR/PulseQt.desktop"

# ── 3. linuxdeployqt ─────────────────────────────────
echo "[3/5] Running linuxdeployqt..."
LINUXDEPLOYQT="$PROJECT_ROOT/linuxdeployqt.AppImage"

if [ ! -x "$LINUXDEPLOYQT" ]; then
    echo "Downloading linuxdeployqt..."
    wget -q -O "$LINUXDEPLOYQT" \
        https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage
    chmod +x "$LINUXDEPLOYQT"
fi

"$LINUXDEPLOYQT" "$APPDIR/usr/bin/PulseQt" -bundle-non-qt-libs -no-translations 2>&1 | tail -3

# ── 4. 清理不必要的 .so（减小体积） ──────────────────
echo "[4/5] Stripping..."
strip "$APPDIR/usr/bin/PulseQt" 2>/dev/null || true

# ── 5. AppImage ──────────────────────────────────────
if $DO_APPIMAGE; then
    echo "[5/5] Generating AppImage..."
    VERSION=$(grep -oP 'VERSION \K[0-9.]+' CMakeLists.txt)
    OUTPUT="PulseQt-${VERSION}-x86_64.AppImage"

    "$LINUXDEPLOYQT" "$APPDIR/usr/bin/PulseQt" -appimage 2>&1 | tail -2

    # linuxdeployqt 输出到当前目录，重命名
    if [ -f "PulseQt-*.AppImage" ]; then
        mv PulseQt-*.AppImage "$OUTPUT" 2>/dev/null || true
    fi

    echo ""
    echo "=== Done: $OUTPUT ==="
    echo "Run: ./$OUTPUT"
else
    echo ""
    echo "=== Done: AppDir at $APPDIR ==="
    echo "Run: $APPDIR/usr/bin/PulseQt"
fi
