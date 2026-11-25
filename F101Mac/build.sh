#!/bin/sh
set -eu
cd "$(dirname "$0")"
app="$PWD/build/F101 Panel.app/Contents"
mkdir -p "$app/MacOS" "$app/Resources"
swiftc -parse-as-library -O Panel.swift ControlPage.swift Navigation.swift -o "$app/MacOS/F101Panel"
cp link.py "$app/Resources/"
cat > "$app/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.f101.macpanel</string>
<key>CFBundleName</key><string>拓竹屏</string>
<key>CFBundleExecutable</key><string>F101Panel</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSUIElement</key><true/>
<key>NSLocalNetworkUsageDescription</key><string>經區網讀取已配對打印機的狀態及相機。</string>
</dict></plist>
PLIST
codesign --force --sign - "$app/.."
