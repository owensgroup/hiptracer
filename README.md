HIPTracer is a library and a CLI utility for recording and playing back sequences of GPU API calls.
This can be used to create a capture or trace that can be stored on disk for later playback, without access to
the original executable. You execute your program with libhiptracer.so in the `LD_PRELOAD` environment variable,
and a capture is created with minimal intrusion into the captured program. We include a command line utility that
sets the proper environment variables for capturing.

# Building
CMake is required to build libhiptracer.so. 

The ordering of /opt/rocm/hip and /opt/rocm matters.
```
mkdir build && cd build
cmake -DCMAKE_HIP_COMPILER=/opt/rocm/bin/hipcc -DCMAKE_PREFIX_PATH=/opt/rocm/hip -DCMAKE_PREFIX_PATH=/opt/rocm ..
```

To build the CLI utility, a Golang compiler is required. The CLI utility is not needed to create captures, but if you
have a Go compiler on your `$PATH`, it will be compiled as part of the CMake build.

# Running
To create a capture, use the CLI utility or set environment variables yourself. 

## CLI Usage
The CLI utility handles setting environment variables for you. 
```
./hiptracer [--output tracer-default.db] [--tracer-library ./libhiptracer.so] <your gpu executable and args>
```
Creates a capture `tracer-default.db` in the current working directory.

## Environment
You can set the environment variables yourself as below:
```
LD_PRELOAD=<path to libhiptracer.so> HIPTRACER_CAPTURE=<capture file> <your program and args>
```

Variables always use a default value if unset:

| ENV Variable        | Default Value       | Use                                       |
| ------------        | ------------------  | ----------------------------------------- |
| `HIPTRACER_CAPTURE` | `tracer-default.db` | Name of capture file                      |
| `HIPTRACER_DEBUG`   | `false`             | Enable debug output during capture (slow) |
| `HIPTRACER_PROGRESS`| `true`              | Display write progress after program exit |


Note: We attempt to run the captured program with minimal intrusion and slowdown, so the majority of capture and all disk access occurs on a separate thread. Larger programs require additional time to write the capture to disk after the captured program exits. This is displayed with a progressbar, which can be disabled with the `HIPTRACER_PROGRESS` variable.

# Storage Format
Captures are stored as a sqlite3 database file that can be viewed with standard tools, ex:
```
sqlite3 tracer-default.db
> SELECT * FROM Events;
...
```
displays all the events in a given capture file. For the event specific data, a separate table with a matching ID is
used. Use `.schema` in sqlite3 for a list of all event types and their corresponding table.

# Examples & Tests
Some tests are included that capture the HIP programs under examples/.

# Results

| Example Program | Capture Size |
| ----------------| ------------ |
| `vectoradd_hip` |    43K       |
| `kripke`        |   422K       |

# Replay

WIP
