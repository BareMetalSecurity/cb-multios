#!/usr/bin/env bash

# Root cb-multios directory
DIR=$(cd "$(dirname ${BASH_SOURCE[0]})" && pwd)
TOOLS="$DIR/tools"

# Install necessary python packages
/usr/bin/env python -c "import yaml; import xlsxwriter"
if [[ $? -ne 0 ]]; then
    echo "Please install pyyaml and xlsxwriter"
    echo "  $ sudo pip install pyyaml xlsxwriter"
    exit 1
fi

echo "Running patcher"
${TOOLS}/cb_patcher.py $@

echo "Generating CMakelists"
${TOOLS}/makefiles.py

echo "Creating build directory"
mkdir ${DIR}/build
cd ${DIR}/build

echo "Creating Makefiles"
cmake -DCMAKE_GENERATE_COMPILE_COMMANDS=On ..

make -j8
