

# Building


The ordering of /opt/rocm/hip and /opt/rocm matters.
```
cmake -DCMAKE_HIP_COMPILER=/opt/rocm/bin/hipcc -DCMAKE_PREFIX_PATH=/opt/rocm/hip -DCMAKE_PREFIX_PATH=/opt/rocm ..
```
