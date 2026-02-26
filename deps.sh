#!/bin/sh
set -e

# Download vendored dependencies

# cJSON (header-only vendoring)
mkdir -p vendor/cJSON
echo "Downloading cJSON..."
curl -sL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.c -o vendor/cJSON/cJSON.c
curl -sL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.h -o vendor/cJSON/cJSON.h
echo "cJSON downloaded to vendor/cJSON/"

echo ""
echo "System dependencies needed (install via package manager):"
echo "  - libcurl (dev headers)   e.g. libcurl4-openssl-dev / libcurl-devel"
echo "  - libyaml (dev headers)   e.g. libyaml-dev / libyaml-devel"
echo ""
echo "On Debian/Ubuntu:  sudo apt install libcurl4-openssl-dev libyaml-dev"
echo "On Fedora/RHEL:    sudo dnf install libcurl-devel libyaml-devel"
echo "On macOS:          brew install curl libyaml"
echo "On Arch:           sudo pacman -S curl libyaml"
