// libFuzzer target for the C++ core (no Ruby involved): decode arbitrary bytes,
// then cluster whatever decodes. Built and run in CI with ASan + UBSan; see the
// "fuzz" job in .github/workflows/ci.yml. To run locally (Linux/macOS, clang):
//
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//     -Iext/palettegen -Iext/palettegen/vendor \
//     fuzz/fuzz_decode.cpp ext/palettegen/image_io.cpp ext/palettegen/kmeans.cpp -o fuzz_decode
//   mkdir -p corpus && cp test/fixtures/* corpus/ && ./fuzz_decode -max_total_time=60 corpus
#include <cstddef>
#include <cstdint>
#include <new>

#include "palettegen.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        // A small pixel cap keeps each run fast and well under libFuzzer's memory limit.
        const palettegen::Image image = palettegen::decode(data, size, 4'000'000);
        palettegen::Options opts;
        opts.colors = 5;
        opts.max_samples = 20000;
        opts.attempts = 1;
        opts.max_iterations = 10;
        palettegen::extract(image, opts);
    } catch (const palettegen::Error&) {
        // Expected for malformed input.
    } catch (const std::bad_alloc&) {
    }
    return 0;
}
