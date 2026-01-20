#!/bin/bash

# NV-HALT Extraction Script
# Run this in your target STM project directory

echo "=== NV-HALT Extraction Script ==="
echo "This script helps extract NV-HALT from ptmbench"
echo

# Set source directory (modify this path)
PTMBENCH_DIR="/home/zkent/ptmbench"

if [ ! -d "$PTMBENCH_DIR" ]; then
    echo "Error: Please set PTMBENCH_DIR to the correct path"
    exit 1
fi

echo "Creating nvhalt/ directory structure..."
mkdir -p nvhalt/phytmlib
mkdir -p nvhalt/tmlib

echo "Copying core NV-HALT files..."
cp -r "$PTMBENCH_DIR/phytmlib/" nvhalt/
cp -r "$PTMBENCH_DIR/tmlib/hytm1/" nvhalt/tmlib/
cp "$PTMBENCH_DIR/user_gstats_handler.h" nvhalt/

echo "Creating minimal example..."
cat > nvhalt/example.cpp << 'EOF'
#include "phytmlib/phytm2.h"
#include <iostream>

struct Counter {
    tx_field<int> value;
    Counter() : value(0) {}
};

int main() {
    TM_STARTUP();
    TM_THREAD_ENTER();
    
    Counter* counter = tmNew<Counter>();
    
    // Increment counter in transaction
    TM_BEGIN();
    counter->value = counter->value + 1;
    std::cout << "Counter: " << (int)counter->value << std::endl;
    TM_END();
    
    tmDelete(counter);
    TM_THREAD_EXIT();
    return 0;
}
EOF

echo "Creating minimal Makefile..."
cat > nvhalt/Makefile << 'EOF'
CXX = g++
CXXFLAGS = -std=c++17 -O2 -DDISABLE_PMEM -Iphytmlib -Itmlib/hytm1
LDFLAGS = -lpthread -lnuma

example: example.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f example

.PHONY: clean
EOF

echo "=== Extraction Complete ==="
echo "Files copied to nvhalt/ directory"
echo "To test: cd nvhalt && make example && ./example"
echo
echo "Integration points:"
echo "1. Include: #include \"phytmlib/phytm2.h\""
echo "2. Use tx_field<T> for transactional data"
echo "3. Wrap operations in TM_BEGIN()/TM_END()"
echo "4. Initialize with TM_STARTUP()/TM_THREAD_ENTER()"