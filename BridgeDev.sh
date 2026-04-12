#!/bin/bash

# 1. Anchor the script to its own directory
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# 2. Setup directory structure
mkdir -p BridgeDev

# 3. Handle Go Module
if [ ! -f "go.mod" ]; then
    echo "Initializing Go Module..."
    go mod init cgo-bridge
fi

# 4. Build Go Engine
# We output to BridgeBuild/libengine.a
echo "Step 1: Building Go Static Archive..."
go build -buildmode=c-archive -o BridgeDev/libengine.a .

# 5. Compile the Final App
# CRITICAL: The order must be Source -> Library -> Pkg-Config
echo "Step 2: Linking C Bridge..."
gcc BridgeDev/bridgedev.c \
    -o my_app \
    -I"$DIR/BridgeDev" \
    -L"$DIR/BridgeDev" \
    -lengine \
    $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1) \
    -lpthread -ldl -lm

echo "------------------------------------"
if [ -f "./my_app" ]; then
    echo "SUCCESS: ./my_app created."
else
    echo "FAILED: Check GCC error output above."
fi