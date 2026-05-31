#include <encoder.h>
#include <decoder.h>

#include <iostream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static fs::path test_file = "free bird.wav";
uint32_t sizes_to_test[] = {
    192,
    256,
    512,
    576,
    1024,
    1152,
    2048,
    2304,
    4096,
    4608,
    8192,
    16384,
};

int main(int argc, char const *argv[])
{
    std::stringstream ss;
    ss << "Frame size;Compression\n";
    size_t initial_size = fs::file_size(test_file);
    for (auto frame_size : sizes_to_test)
    {
        std::vector<uint8_t> out(2 * initial_size);
        FlacEncoder f(out);
        size_t final_size = f.encode(test_file, frame_size);
        auto compression = (double)initial_size / (double)final_size;
        ss << frame_size << "; " << compression << "\n";

        std::cout << "Frame size " << frame_size << " resulted in " << compression << " compression ratio\n";
    }
    std::ofstream benchmark_fs("benchmark.csv");
    benchmark_fs << ss.rdbuf();
    return 0;
}
