#include "emberfall/scene.hpp"

#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace ember {

std::string read_bounded(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open " + path.string());
    std::string result(limit + 1, '\0');
    file.read(result.data(), static_cast<std::streamsize>(result.size()));
    if (file.bad() || !file.eof()) throw std::runtime_error("Unreadable or oversized " + path.string());
    result.resize(static_cast<std::size_t>(file.gcount()));
    return result;
}

void write_atomic(const std::filesystem::path& path, std::string_view bytes) {
    auto pattern = path.string() + ".XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    int fd = mkstemp(temporary.data());
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create " + pattern);
    const auto checked = [](bool success, const char* operation) {
        if (!success) throw std::system_error(errno, std::generic_category(), operation);
    };
    try {
        while (!bytes.empty()) {
            const auto written = ::write(fd, bytes.data(), bytes.size());
            if (written < 0 && errno == EINTR) continue;
            checked(written > 0, "Write temporary file");
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
        checked(::fsync(fd) == 0, "Sync temporary file");
        const auto close_result = ::close(fd);
        fd = -1;
        checked(close_result == 0, "Close temporary file");
        checked(::rename(temporary.data(), path.c_str()) == 0, "Publish temporary file");
        const auto dir = path.has_parent_path() ? path.parent_path() : ".";
        fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
        checked(fd >= 0, "Open containing directory");
        checked(::fsync(fd) == 0, "Sync containing directory");
        ::close(fd);
    } catch (...) {
        if (fd >= 0) ::close(fd);
        ::unlink(temporary.data());
        throw;
    }
}

}
