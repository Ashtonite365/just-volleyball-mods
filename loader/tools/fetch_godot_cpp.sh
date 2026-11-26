#!/usr/bin/env bash




set -e
TAG="${TAG:-godot-4.5-stable}"
cd "$(dirname "$0")/.."
[ -d thirdparty/godot-cpp ] || git clone --depth 1 -b "$TAG" https://github.com/godotengine/godot-cpp thirdparty/godot-cpp
