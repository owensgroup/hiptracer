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
make -j4
```

The capture library, replay tool, and CLI utility will all be built after this process.

# Running
To create a capture, use the CLI utility or set environment variables yourself before running your GPU program.

## CLI Usage
The CLI utility handles setting environment variables for you. Make sure to use the `--` or "double-dash" when
your program has its own arguments separate from hiptracer.
```
./hiptracer [-o tracer-default.db] [-l ./libhiptracer.so] -- <your gpu executable and args>
```
Creates a capture `tracer-default.db` in the current working directory. For example,

```
$ ./hiptracer ../examples/vectorAdd/vectoradd_hip.exe
 System minor 0
 System major 9
 agent prop name
hip Device prop succeeded
FREE 34330378240
TOTAL 34342961152
EVENTS REMAINING 6
[        #         #         #         #        #] 100%
CAPTURE COMPLETE
```

A progressbar that indicates capture progress is displayed after the program exits. Writing captures should
have no impact on the actual program by deferring most writing till after it exits.

## Environment
You can set the environment variables yourself as below:
```
LD_PRELOAD=<path to libhiptracer.so> HIPTRACER_EVENTDB=<capture file> <your program and args>
```

Variables always use a default value if unset:

| ENV Variable        | Default Value       | Use                                       |
| ------------        | ------------------  | ----------------------------------------- |
| `HIPTRACER_EVENTDB` | `tracer-default.db` | Name of capture file                      |
| `HIPTRACER_DEBUG`   | `false`             | Enable debug output during capture |
| `HIPTRACER_SKIPHOSTDATA`| `false`              | Skip writing host data during capture, (improves capture speed, but makes complete replay impossible) | 


Note: We attempt to run the captured program with minimal intrusion and slowdown, so the majority of capture and all disk access occurs on a separate thread. Larger programs require additional time to write the capture to disk after the captured program exits. Host side arrays that were copied to the GPU are the largest source of capture time. If you'd like to skip
capturing this data because you already have it or don't need it, you can set the `SKIPHOSTDATA` environment variable.

# Storage Format
Captures are stored as a sqlite3 database file that can be viewed with standard tools, ex:
```
sqlite3 tracer-default.db
> SELECT * FROM Events;
...
```
displays all the events in a given capture file. For the event specific data, a separate table with a matching ID is
used. Ex: Malloc events have a table EventsMalloc, where additional information like `dst` and `src` are stored. Use `.schema` in sqlite3 for a list of all event types and their corresponding table and column information.

# Examples & Tests
Some tests are included that capture the HIP programs under examples/. Functionality is a WIP as new programs are enabled and bugs are fixed. Tests can be run under CMake with ctest after building.

# Results
Capture sizes may be inaccurate if replay doesn't function.
Replay indicates current status of replay tool with the program.
Some programs don't replay because of how they access GPU memory (through opaque structs) - this can be addressed with GPU virtual memory management.
Some programs don't replay because of unknown kernel argument types.

| Example Program | Capture Size | Replay Functionality | Comments |
| ----------------| ------------ | -------------------  | -------- |
| `vectoradd_hip` |   2.6M       | ✅ | |
| `cuda-stream`   |    121K       | ✅  | |
| `strided-access` |12K          | ✅   | |
| `reduction`     |  13M          | ✅ | |
| `pytorch-word_language_model` | 305M | ❌ | Unknown kernel argument type "hidden" |
| `kripke`        |   2.1M        | ❌ | Need VirtualMem to replay |

# Issues

* Replay is very problematic right now. Simple sequences of API calls should work, but some types of kernel launches will fail.
* Only supports gfx908.
