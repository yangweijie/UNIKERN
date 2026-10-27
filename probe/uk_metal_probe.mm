// Phase 10a: Metal environment probe — ObjC++ linking Metal.framework
// (CommandLineTools SDK, no Xcode needed). Verifies:
//   1. MTLDevice creation
//   2. newLibraryWithSource: JIT-compiles MSL at runtime (metal_stdlib
//      resolves inside Metal.framework's built-in compiler)
//   3. Buffer allocation + argument encoding + dispatch + readback
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

int main() {
  @autoreleasepool {
    NSError* err = nil;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
      NSLog(@"UK_PROBE_FAIL no_device");
      return 1;
    }
    NSLog(@"UK_DEVICE name=%@", dev.name);

    id<MTLCommandQueue> queue = [dev newCommandQueue];
    if (!queue) { NSLog(@"UK_PROBE_FAIL no_queue"); return 1; }

    // Trivial MSL kernel: C = A * 2
    NSString* src = @"#include <metal_stdlib>\n"
                     "using namespace metal;\n"
                     "kernel void uk_probe(device const float* A [[buffer(0)]],\n"
                     "                     device float* C [[buffer(1)]],\n"
                     "                     constant int& N [[buffer(2)]],\n"
                     "                     uint tid [[thread_position_in_grid]]) {\n"
                     "  if (tid < N) C[tid] = A[tid] * 2.0f;\n"
                     "}\n";
    id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
      NSLog(@"UK_PROBE_FAIL compile err=%@", err);
      return 1;
    }
    NSLog(@"UK_COMPILE OK (runtime JIT)");
    id<MTLFunction> fn = [lib newFunctionWithName:@"uk_probe"];
    if (!fn) { NSLog(@"UK_PROBE_FAIL no_function"); return 1; }

    id<MTLComputePipelineState> ps =
        [dev newComputePipelineStateWithFunction:fn error:&err];
    if (!ps) {
      NSLog(@"UK_PROBE_FAIL pipeline err=%@", err);
      return 1;
    }

    const int N = 256;
    NSMutableData* ad = [NSMutableData dataWithLength:N * sizeof(float)];
    float* a = (float*)ad.mutableBytes;
    for (int i = 0; i < N; ++i) a[i] = (float)i;

    id<MTLBuffer> bufA = [dev newBufferWithBytes:ad.mutableBytes
                                       length:N * sizeof(float)
                                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> bufC = [dev newBufferWithLength:N * sizeof(float)
                                     options:MTLResourceStorageModeShared];

    id<MTLCommandBuffer> cb = [queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:ps];
    [enc setBuffer:bufA offset:0 atIndex:0];
    [enc setBuffer:bufC offset:0 atIndex:1];
    int nval = N;
    [enc setBytes:&nval length:sizeof(int) atIndex:2];
    MTLSize grid = MTLSizeMake(N, 1, 1);
    MTLSize tgroup = MTLSizeMake(256, 1, 1);
    [enc dispatchThreads:grid threadsPerThreadgroup:tgroup];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    float* c = (float*)[bufC contents];
    int bad = 0;
    for (int i = 0; i < N; ++i) if (c[i] != a[i] * 2.0f) ++bad;
    NSLog(@"UK_PROBE_RESULT verify=%s bad=%d", bad == 0 ? "PASS" : "FAIL", bad);
    NSLog(@"UK_PROBE sample c[0]=%f c[1]=%f c[255]=%f", c[0], c[1], c[255]);
    return bad == 0 ? 0 : 1;
  }
}
