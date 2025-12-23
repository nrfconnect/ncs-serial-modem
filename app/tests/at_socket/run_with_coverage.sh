#!/bin/bash
#
# Script to build and run tests with coverage
#

set -e

# Clean and build with coverage
echo "Building with coverage..."
west build -b native_sim -p -- -DCONFIG_COVERAGE=y

# Run tests
echo "Running tests..."
./build/at_socket/zephyr/zephyr.exe

# Generate coverage
echo "Generating coverage report..."
lcov --capture --directory build/at_socket --output-file build/at_socket/coverage.info \
     --exclude '*/zephyr/*' \
     --exclude '*/test/*' \
     --exclude '*/mocks/*' \
     --exclude '*/unity/*' --ignore-errors unused

# Generate HTML report
genhtml build/at_socket/coverage.info --output-directory build/at_socket/coverage_html

echo ""
echo "Coverage report generated in build/at_socket/coverage_html/index.html"
echo ""

# Show coverage summary for sm_at_socket.c
echo "Coverage summary:"
lcov --list build/at_socket/coverage.info
