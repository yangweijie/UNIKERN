# third_party/CL — OpenCL C API headers (vendored)

Provenance: Khronos OpenCL-Headers, distributed via conda-forge
`opencl-headers` 2025.06.13 build h826ebb9_0 (win-64), extracted 2026-10-27
for Phase 9 (Intel Arc B390 real-machine validation). The oneAPI 2025.3/2026.1
installs on this host ship only a SYCL-flavored minimal CL set (no cl_device.h;
CL_DEVICE_UNIFIED_MEMORY absent), so the full headers are vendored here instead
of depending on a specific oneAPI version.

- License: Apache-2.0 (see LICENSE-APACHE-2.0.txt)
- Layout: monolithic umbrella style — `cl.h` contains the core API, plus
  cl_platform.h / cl_version.h / cl_ext.h / cl_half.h / cl_gl*.h / cl_icd.h etc.
  `CL_TARGET_OPENCL_VERSION` defaults to 300 when undefined.
- Include as `#include <CL/cl.h>` with this directory on the include path.
- Do not edit: update by re-extracting a newer conda-forge opencl-headers
  package and diffing against Khronos upstream.
