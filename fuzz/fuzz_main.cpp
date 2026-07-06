#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

void set_stdin_binary_mode() noexcept
{
#ifdef _WIN32
    (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
}

std::vector<std::uint8_t> read_all(std::istream& input)
{
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc <= 1) {
        set_stdin_binary_mode();
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
