#!/bin/bash
# Run this script in its own folder (hardcore bashism)
DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd $DIR
echo "Removing existing hex file..."
find -name '*.hex' -exec rm -v {} \;
