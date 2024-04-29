#!/bin/bash

# Set the path to your Python script
python_script="build_scripts/build_usd.py"

# Set the target directory where the artifacts are generated
target_directory="../build-wasm"

# Set the destination directory where you want to copy the files
destination_directory="/Users/andrewbeers/git/needle/usd-viewer/public"

# Run the Python script with the specified arguments
python3 "$python_script" --build-target wasm "$target_directory"

# Check if the Python script executed successfully
if [ $? -eq 0 ]; then

    # Copy the desired files from the target directory to the destination directory
    cp "$target_directory/bin/emHdBindings.data" "$destination_directory"
    cp "$target_directory/bin/emHdBindings.js" "$destination_directory"
    cp "$target_directory/bin/emHdBindings.wasm" "$destination_directory"
    cp "$target_directory/bin/emHdBindings.worker.js" "$destination_directory"

    say ready
else
    say failed
fi