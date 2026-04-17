mkdir build
cd build
cmake -S .. -B . -A x64
cmake --build . --config Release
cd ..
