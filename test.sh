#!/bin/bash

echo -e "Creating test_file\n"
echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor
incididunt ut labore et dolore magna aliqua." > test_file

gcc vfs.c -o vfs
echo -e "Creating new virtual disk...\n"
./vfs create disk 20000

echo "Displaying disk map..."
./vfs map disk

echo -e "\nCreating 3 files from test_file content...\n"
./vfs write disk test_file file1
./vfs write disk test_file file2
./vfs write disk test_file file3

echo -e "Displaying directory..."
./vfs ls disk

echo -e "\nDisplaying disk map..."
./vfs map disk

echo -e "\nRemoving file2...\n"
./vfs rm disk file2

echo "Displaying directory..."
./vfs ls disk

echo -e "\nDisplaying disk map..."
./vfs map disk

echo -e "\nCreating host file..."
./vfs read disk file1 new_host_file
