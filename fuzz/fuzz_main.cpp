#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

std::vector<std::uint8_t> read_all(std::istream& input)
{
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc <= 1) {
        const auto data = read_all(std::cin);
        return LLVMFuzzerTestOneInput(data.data(), data.size());
    }

    for (int index = 1; index < argc; ++index) {
        std::ifstream input(argv[index], std::ios::binary);
        if (!input) {
            std::cerr << "failed to open fuzz input: " << argv[index] << '\n';
            return 2;
        }
        const auto data = read_all(input);
        const int result = LLVMFuzzerTestOneInput(data.data(), data.size());
        if (result != 0) {
            return result;
        }
    }
    return 0;
}
