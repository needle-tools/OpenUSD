#!/bin/bash

# Function to display usage
usage() {
    echo "Usage: $0 [--mode debug|release --build-dir <dir> --destination-dir <dir>]"
    exit 1
}

# Check for the mode argument
if [ "$#" -ne 6 ]; then
  echo "$#"
  usage
fi

mode=$2
build_dir=$4
destination_directory=$6

# Validate the mode argument
if [ "$mode" != "debug" ] && [ "$mode" != "release" ]; then
    usage
fi

if [ "$mode" != "release" ]; then
  python3 build_scripts/build_usd.py --build-target wasm --build-variant debug "$build_dir"
else
  python3 build_scripts/build_usd.py --build-target wasm "$build_dir"
fi

# Check if the Python script executed successfully
if [ $? -eq 0 ]; then

  if [ "$mode" != "release" ]; then
      cp "$build_dir/bin/emHdBindings.wasm" "$destination_directory/emHdBindings.wasm"
  else
      wasm-opt -Oz -o "$destination_directory/emHdBindings.wasm" "$build_dir/bin/emHdBindings.wasm" --enable-bulk-memory --enable-threads
  fi
  # Copy the desired files from the target directory to the destination directory
  cp "$build_dir/bin/emHdBindings.data" "$destination_directory"
  cp "$build_dir/bin/emHdBindings.js" "$destination_directory/emHdBindings.js"
  cp "$build_dir/bin/emHdBindings.worker.js" "$destination_directory"

  prettier --write "$destination_directory/emHdBindings.js"

  patch "$destination_directory/emHdBindings.js" < "$destination_directory/patches/arguments_1.patch"
  # TODO: fix this
  # patch "$destination_directory/emHdBindings.js" < "$destination_directory/patches/arguments_2.patch"
  patch "$destination_directory/emHdBindings.js" < "$destination_directory/patches/abort.patch"
  # TODO: fix this
  # patch "$destination_directory/emHdBindings.js" < "$destination_directory/patches/fileSystem.patch"

  say ready
else
  say failed
fi