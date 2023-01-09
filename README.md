HIPTracer is a set of libraries and a CLI utility for recording and playing back sequences of GPU API calls to AMD's HIP Runtime.

# Usage
```
./hiptracer [--output tracer-default.db] [--tracer-library ./libhiptracer.so] <your gpu executable>
```
Creates a capture `tracer-default.db` in the current working directory.


# Building


The ordering of /opt/rocm/hip and /opt/rocm matters.
```
cmake -DCMAKE_HIP_COMPILER=/opt/rocm/bin/hipcc -DCMAKE_PREFIX_PATH=/opt/rocm/hip -DCMAKE_PREFIX_PATH=/opt/rocm ..
```
