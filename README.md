# Gang Malloc

## Building
```sh
mkdir build
CC=/path/to/mpsp/libc/bin/musl-gcc cmake -DCMAKE_INSTALL_PREFIX=/path/to/mpsp/libc/ ..
make install
```

## Testing
```sh
mkdir build
CC=/path/to/mpsp/libc/bin/musl-gcc cmake -DCMAKE_INSTALL_PREFIX=/path/to/mpsp/libc/ ..
make stress-tests
./stress-tests
```

## Compiler Flags
* Configure timeout (in milliseconds) by using BARRIER_WAIT_TIME=N
* Configure gang size (number of threads) by using MAX_GANG_SIZE=N
* On MPSP, enable MPSP_GLIBC=1
* On MPSP, specify number of CPU cores NUM_CORES=N
