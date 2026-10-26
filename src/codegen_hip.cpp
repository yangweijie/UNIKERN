// AMD HIP backend: emit a complete, self-verifying, self-timing program (R5+R6).
// The emitted program uses <iostream> only, so this generator contains no
// backslash escapes at all (they do not survive every editing path reliably).
#include <sstream>

#include "unikern/codegen.hpp"
#include "unikern/components.hpp"

namespace unikern {

EmittedTile emit_tile_for(const KmlTree& tree, const Solution& sol) {
  auto clamp16 = [](int v, int lo, int hi) {
    int c = v < lo ? lo : (v > hi ? hi : v);
    return (c / 16) * 16;
  };
  EmittedTile t;
  t.tm = clamp16(sol.best.tile_m ? sol.best.tile_m : tree.tile_m, 32, 96);
  t.tn = clamp16(sol.best.tile_n ? sol.best.tile_n : tree.tile_n, 32, 96);
  t.clamped = (t.tm != sol.best.tile_m) || (t.tn != sol.best.tile_n);
  return t;
}

std::string hip_runnable_program(const Solution& sol, const Hcs& hcs, const KmlTree& tree) {
  const EmittedTile tile = emit_tile_for(tree, sol);
  const int RPT = tile.tm / 16;
  const int RPN = tile.tn / 16;
  const int TK = 16;
  const int VM = 256, VN = 256, VK = 256;          // differential verification size
  int PM = 1024, PN = 1024, PK = 1024;             // timing size (capped)
  if (tree.op == OpKind::Gemm) {
    PM = tree.dims[0] < 1024 ? (tree.dims[0] / 16) * 16 : 1024;
    PN = tree.dims[1] < 1024 ? (tree.dims[1] / 16) * 16 : 1024;
    PK = tree.dims[2] < 1024 ? (tree.dims[2] / 16) * 16 : 1024;
    if (PM < 32) PM = 32;
    if (PN < 32) PN = 32;
    if (PK < 32) PK = 32;
  }

  std::ostringstream o;
  o << "// unikern generated — AMD HIP / RDNA (independent generation, R5.1)" << '\n'
    << "// hcs=" << hcs.vendor << "/" << hcs.device
    << " fp=" << hcs_fingerprint(hcs).substr(0, 12)
    << " constants_v=" << sol.constants_version << '\n'
    << "// solved tile=" << sol.best.tile_m << "x" << sol.best.tile_n
    << " -> emitted block tile=" << tile.tm << "x" << tile.tn
    << (tile.clamped ? " (clamped for register budget)" : "") << '\n'
    << "// placement: " << sol.placement_record << '\n'
    << "#include <hip/hip_runtime.h>" << '\n'
    << "#include <cmath>" << '\n'
    << "#include <iomanip>" << '\n'
    << "#include <iostream>" << '\n'
    << "#include <vector>" << '\n' << '\n'
    << "#define TM " << tile.tm << '\n'
    << "#define TN " << tile.tn << '\n'
    << "#define TK " << TK << '\n'
    << "#define RPT " << RPT << '\n'
    << "#define RPN " << RPN << '\n' << '\n'
    << "__global__ void uk_gemm(const float* __restrict__ A, const float* __restrict__ B," << '\n'
    << "                        float* __restrict__ C, int M, int N, int K) {" << '\n'
    << "  __shared__ float As[TK][TM];" << '\n'
    << "  __shared__ float Bs[TK][TN];" << '\n'
    << "  const int bm = blockIdx.y * TM, bn = blockIdx.x * TN;" << '\n'
    << "  const int tx = threadIdx.x, ty = threadIdx.y;" << '\n'
    << "  float acc[RPT][RPN];" << '\n'
    << "  for (int r = 0; r < RPT; ++r)" << '\n'
    << "    for (int c = 0; c < RPN; ++c) acc[r][c] = 0.f;" << '\n'
    << "  for (int k0 = 0; k0 < K; k0 += TK) {" << '\n'
    << "    for (int i = ty; i < TK; i += 16)" << '\n'
    << "      for (int j = tx; j < TM; j += 16)" << '\n'
    << "        As[i][j] = (bm + j < M && k0 + i < K) ? A[(size_t)(bm + j) * K + k0 + i] : 0.f;" << '\n'
    << "    for (int i = ty; i < TK; i += 16)" << '\n'
    << "      for (int j = tx; j < TN; j += 16)" << '\n'
    << "        Bs[i][j] = (k0 + i < K && bn + j < N) ? B[(size_t)(k0 + i) * N + bn + j] : 0.f;" << '\n'
    << "    __syncthreads();" << '\n'
    << "    for (int k = 0; k < TK; ++k) {" << '\n'
    << "      float a[RPT], b[RPN];" << '\n'
    << "      for (int r = 0; r < RPT; ++r) a[r] = As[k][ty + r * 16];" << '\n'
    << "      for (int c = 0; c < RPN; ++c) b[c] = Bs[k][tx + c * 16];" << '\n'
    << "      for (int r = 0; r < RPT; ++r)" << '\n'
    << "        for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];" << '\n'
    << "    }" << '\n'
    << "    __syncthreads();" << '\n'
    << "  }" << '\n'
    << "  for (int r = 0; r < RPT; ++r)" << '\n'
    << "    for (int c = 0; c < RPN; ++c) {" << '\n'
    << "      const int row = bm + ty + r * 16, col = bn + tx + c * 16;" << '\n'
    << "      if (row < M && col < N) C[(size_t)row * N + col] = acc[r][c];" << '\n'
    << "    }" << '\n'
    << "}" << '\n' << '\n'
    << "// Deterministic init, bit-identical to the CPU reference." << '\n'
    << "static float initA(int idx) { return ((idx % 17) - 8) * 0.0625f; }" << '\n'
    << "static float initB(int idx) { return ((idx % 13) - 6) * 0.0625f; }" << '\n' << '\n'
    << "static void cpu_ref(const std::vector<float>& A, const std::vector<float>& B," << '\n'
    << "                    std::vector<float>& C, int M, int N, int K) {" << '\n'
    << "  for (int i = 0; i < M; ++i)" << '\n'
    << "    for (int j = 0; j < N; ++j) {" << '\n'
    << "      float s = 0.f;" << '\n'
    << "      for (int k = 0; k < K; ++k) s += A[(size_t)i * K + k] * B[(size_t)k * N + j];" << '\n'
    << "      C[(size_t)i * N + j] = s;" << '\n'
    << "    }" << '\n'
    << "}" << '\n' << '\n'
    << "// Every HIP call is checked: a launch that fails must abort with a" << '\n'
    << "// UNIKERN_ERROR line instead of leaving zeroed device memory for the" << '\n'
    << "// comparison to silently misread as a wrong answer." << '\n'
    << "static bool uk_ok(hipError_t e, const char* what) {" << '\n'
    << "  if (e == hipSuccess) return true;" << '\n'
    << "  std::cerr << \"UNIKERN_ERROR what=\" << what" << '\n'
    << "            << \" msg=\" << hipGetErrorString(e) << std::endl;" << '\n'
    << "  return false;" << '\n'
    << "}" << '\n' << '\n'
    << "int main() {" << '\n'
    << "  const int VM = " << VM << ", VN = " << VN << ", VK = " << VK << ";" << '\n'
    << "  const int PM = " << PM << ", PN = " << PN << ", PK = " << PK << ";" << '\n'
    << "  int rc = 0;" << '\n'
    << "  float max_rel = 0.f;" << '\n'
    << "  {" << '\n'
    << "    std::vector<float> A((size_t)VM * VK), B((size_t)VK * VN);" << '\n'
    << "    std::vector<float> C((size_t)VM * VN), R((size_t)VM * VN);" << '\n'
    << "    for (size_t i = 0; i < A.size(); ++i) A[i] = initA((int)i);" << '\n'
    << "    for (size_t i = 0; i < B.size(); ++i) B[i] = initB((int)i);" << '\n'
    << "    cpu_ref(A, B, R, VM, VN, VK);" << '\n'
    << "    float *dA, *dB, *dC;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dA, A.size() * sizeof(float)), \"hipMalloc dA\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dB, B.size() * sizeof(float)), \"hipMalloc dB\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dC, C.size() * sizeof(float)), \"hipMalloc dC\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy A\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy B\")) return 4;" << '\n'
    << "    dim3 blk(16, 16);" << '\n'
    << "    dim3 grd((VN + TN - 1) / TN, (VM + TM - 1) / TM);" << '\n'
    << "    uk_gemm<<<grd, blk>>>(dA, dB, dC, VM, VN, VK);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"sync verify\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(C.data(), dC, C.size() * sizeof(float), hipMemcpyDeviceToHost), \"hipMemcpy C\")) return 4;" << '\n'
    << "    for (size_t i = 0; i < C.size(); ++i) {" << '\n'
    << "      float den = std::fabs(R[i]);" << '\n'
    << "      if (den < 1e-6f) den = 1e-6f;" << '\n'
    << "      float rel = std::fabs(C[i] - R[i]) / den;" << '\n'
    << "      if (rel > max_rel) max_rel = rel;" << '\n'
    << "    }" << '\n'
    << "    if (max_rel > 0.03125f) rc = 1;" << '\n'
    << "    hipFree(dA); hipFree(dB); hipFree(dC);" << '\n'
    << "  }" << '\n'
    << "  double ms = 0.0;" << '\n'
    << "  {" << '\n'
    << "    std::vector<float> A((size_t)PM * PK, 0.5f), B((size_t)PK * PN, 0.25f);" << '\n'
    << "    std::vector<float> C((size_t)PM * PN, 0.f);" << '\n'
    << "    float *dA, *dB, *dC;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dA, A.size() * sizeof(float)), \"hipMalloc tA\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dB, B.size() * sizeof(float)), \"hipMalloc tB\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMalloc(&dC, C.size() * sizeof(float)), \"hipMalloc tC\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dA, A.data(), A.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy tA\")) return 4;" << '\n'
    << "    if (!uk_ok(hipMemcpy(dB, B.data(), B.size() * sizeof(float), hipMemcpyHostToDevice), \"hipMemcpy tB\")) return 4;" << '\n'
    << "    dim3 blk(16, 16);" << '\n'
    << "    dim3 grd((PN + TN - 1) / TN, (PM + TM - 1) / TM);" << '\n'
    << "    hipEvent_t t0, t1;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t0), \"hipEventCreate t0\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventCreate(&t1), \"hipEventCreate t1\")) return 4;" << '\n'
    << "    uk_gemm<<<grd, blk>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch warmup\")) return 4;" << '\n'
    << "    for (int i = 1; i < 2; ++i) uk_gemm<<<grd, blk>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "    if (!uk_ok(hipDeviceSynchronize(), \"warmup sync\")) return 4;" << '\n'
    << "    const int iters = 10;" << '\n'
    << "    if (!uk_ok(hipEventRecord(t0), \"hipEventRecord t0\")) return 4;" << '\n'
    << "    for (int i = 0; i < iters; ++i) uk_gemm<<<grd, blk>>>(dA, dB, dC, PM, PN, PK);" << '\n'
    << "    if (!uk_ok(hipGetLastError(), \"launch timed\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventRecord(t1), \"hipEventRecord t1\")) return 4;" << '\n'
    << "    if (!uk_ok(hipEventSynchronize(t1), \"hipEventSynchronize t1\")) return 4;" << '\n'
    << "    float elapsed = 0.f;" << '\n'
    << "    if (!uk_ok(hipEventElapsedTime(&elapsed, t0, t1), \"hipEventElapsedTime\")) return 4;" << '\n'
    << "    ms = double(elapsed) / iters;" << '\n'
    << "    hipEventDestroy(t0); hipEventDestroy(t1);" << '\n'
    << "    hipFree(dA); hipFree(dB); hipFree(dC);" << '\n'
    << "  }" << '\n'
    << "  std::cout << \"UNIKERN_RESULT backend=amd verify=\" << (rc == 0 ? \"PASS\" : \"FAIL\")" << '\n'
    << "            << \" max_rel=\" << std::scientific << std::setprecision(6) << (double)max_rel" << '\n'
    << "            << std::fixed << std::setprecision(6)" << '\n'
    << "            << \" measured_ms=\" << ms" << '\n'
    << "            << \" perf=\" << PM << \"x\" << PN << \"x\" << PK << std::endl;" << '\n'
    << "  return rc;" << '\n'
    << "}" << '\n';
  return o.str();
}

}  // namespace unikern
