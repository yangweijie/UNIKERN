// Runner (R5.2/R6): compile + execute a generated program, parse the result
// contract line "UNIKERN_RESULT backend=... verify=... max_rel=... measured_ms=...".
// Paths use forward slashes throughout: valid on Windows and POSIX, and immune
// to the backslash-mangling editing path that broke this file before (E10).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "unikern/codegen.hpp"

#ifdef _WIN32
#define UK_POPEN _popen
#define UK_PCLOSE _pclose
#else
#define UK_POPEN popen
#define UK_PCLOSE pclose
#endif

namespace unikern {

namespace {

std::string run_capture(const std::string& cmd, int* exit_code) {
  std::string out;
  FILE* p = UK_POPEN(cmd.c_str(), "r");
  if (!p) {
    if (exit_code) *exit_code = -1;
    return out;
  }
  std::array<char, 512> buf{};
  while (std::fgets(buf.data(), int(buf.size()), p)) out += buf.data();
  const int rc = UK_PCLOSE(p);
  if (exit_code) *exit_code = rc;
  return out;
}

// Parse "key=value" tokens from the UNIKERN_RESULT line.
std::string field(const std::string& line, const std::string& key) {
  const std::string pat = key + "=";
  const auto p = line.find(pat);
  if (p == std::string::npos) return {};
  const auto start = p + pat.size();
  const auto e = line.find_first_of(" \t\r\n", start);
  return line.substr(start, e == std::string::npos ? std::string::npos : e - start);
}

}  // namespace

// Host-only HIP probe (zero __global__ functions, so hipcc's default offload
// target cannot matter): prints one "UK_ARCH <gcnArchName>" line per visible
// device. The runner feeds those names to the real build as --offload-arch
// flags. Without them hipcc defaults to gfx906; on any other device every
// kernel launch then fails with "device kernel image is invalid" (error 200)
// while hipDeviceSynchronize still reports success — the silent zero-output
// failure that produced verify=FAIL max_rel=1 with a ~0 ms measurement.
const char* kArchProbeSrc = R"PROBE(#include <hip/hip_runtime.h>
#include <iostream>
int main() {
  int n = 0;
  hipError_t e = hipGetDeviceCount(&n);
  if (e != hipSuccess || n <= 0) {
    std::cout << "UK_ARCH_FAIL " << hipGetErrorString(e)
              << " count=" << n << std::endl;
    return 1;
  }
  for (int d = 0; d < n; ++d) {
    hipDeviceProp_t p{};
    e = hipGetDeviceProperties(&p, d);
    if (e != hipSuccess) {
      std::cout << "UK_ARCH_FAIL " << hipGetErrorString(e) << std::endl;
      return 1;
    }
    std::cout << "UK_ARCH " << p.gcnArchName << std::endl;
  }
  return 0;
}
)PROBE";

// Compile + run the arch probe, parse "UK_ARCH <arch>" lines (deduplicated,
// order kept). Returns false with a diagnostic in *log when no arch is found.
bool probe_gpu_arches(const std::string& hipcc, const std::string& workdir,
                      std::vector<std::string>* arches, std::string* log) {
  namespace fs = std::filesystem;
  const std::string src = workdir + "/uk_arch_probe.cpp";
  const std::string exe = workdir + "/uk_arch_probe.exe";
  {
    std::ofstream f(src, std::ios::binary);
    if (!f) {
      *log = "cannot write " + src;
      return false;
    }
    f << kArchProbeSrc;
  }
  int rc = 0;
  const std::string build_cmd =
      "\"\"" + hipcc + "\" \"" + src + "\" -O2 -o \"" + exe + "\" 2>&1\"";
  const std::string build_out = run_capture(build_cmd, &rc);
  if (rc != 0) {
    *log = "arch probe compile failed (rc=" + std::to_string(rc) + "): " + build_out;
    return false;
  }
  const std::string out = run_capture("\"\"" + exe + "\" 2>&1\"", &rc);
  if (rc != 0) {
    *log = "arch probe run failed (rc=" + std::to_string(rc) + "): " + out;
    return false;
  }
  std::istringstream iss(out);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string tag = "UK_ARCH ";
    if (line.compare(0, tag.size(), tag) == 0) {
      const std::string arch = line.substr(tag.size());
      if (std::find(arches->begin(), arches->end(), arch) == arches->end())
        arches->push_back(arch);
    }
  }
  if (arches->empty()) {
    *log = "arch probe found no device (output: " + out + ")";
    return false;
  }
  return true;
}

std::string find_hipcc() {
  const char* hip = std::getenv("HIP_PATH");
  if (!hip || !*hip) return {};
  std::string base = hip;
  while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
  namespace fs = std::filesystem;
  const fs::path cands[] = {fs::path(base) / "bin" / "hipcc.exe",
                            fs::path(base) / "hipcc.exe"};
  for (const auto& c : cands) {
    std::error_code ec;
    if (fs::exists(c, ec)) return c.generic_string();  // forward slashes
  }
  return {};
}

RunOutcome run_hip(const std::string& program, const std::string& workdir) {
  RunOutcome r;
  const std::string hipcc = find_hipcc();
  if (hipcc.empty()) {
    r.log = "hipcc not found (HIP_PATH unset or bin/hipcc.exe missing)";
    return r;
  }
  std::error_code ec;
  std::filesystem::create_directories(workdir, ec);
  const std::string src = workdir + "/uk_amd.cpp";
  const std::string exe = workdir + "/uk_amd.exe";
  {
    std::ofstream f(src, std::ios::binary);
    if (!f) {
      r.log = "cannot write " + src;
      return r;
    }
    f << program;
  }

  int rc = 0;
  std::vector<std::string> arches;
  std::string probe_log;
  if (!probe_gpu_arches(hipcc, workdir, &arches, &probe_log)) {
    r.log = "GPU arch probe failed: " + probe_log;
    return r;
  }
  std::string arch_flags;
  for (const auto& a : arches) arch_flags += " --offload-arch=" + a;
  const std::string build_cmd =
      "\"\"" + hipcc + "\" \"" + src + "\" -O3" + arch_flags + " -o \"" + exe +
      "\" 2>&1\"";
  const std::string build_out = run_capture(build_cmd, &rc);
  r.log = "offload-arch:";
  for (const auto& a : arches) r.log += " " + a;
  r.log += "\n" + build_out;
  if (rc != 0) {
    r.log = "compile failed (rc=" + std::to_string(rc) + "): " + build_out;
    return r;
  }
  r.built = true;

  const std::string run_cmd = "\"\"" + exe + "\" 2>&1\"";
  const std::string out = run_capture(run_cmd, &rc);
  r.log += out;
  if (rc != 0) r.log += "(exit=" + std::to_string(rc) + ")";
  r.ran = true;

  const auto p = out.find("UNIKERN_RESULT");
  if (p == std::string::npos) {
    r.log += "\n(no UNIKERN_RESULT line)";
    return r;
  }
  const auto nl = out.find('\n', p);
  const std::string line =
      out.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
  r.verified = field(line, "verify") == "PASS";
  r.max_rel = std::atof(field(line, "max_rel").c_str());
  r.measured_ms = std::atof(field(line, "measured_ms").c_str());
  r.kernel_ms = std::atof(field(line, "kernel_ms").c_str());
  // R8.5: migrations=<n> (non-device-resident allocation count). Absent token
  // leaves the -1 sentinel — the RUN line then omits the field instead of
  // printing an unmeasured 0.
  const std::string mig = field(line, "migrations");
  if (!mig.empty()) r.migrations = std::atoi(mig.c_str());
  return r;
}

}  // namespace unikern
