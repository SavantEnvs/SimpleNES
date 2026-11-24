#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace mayhem
{
// The project's loaders only take a path: stage the input in $TMPDIR (default /tmp).
struct ScratchFile
{
    std::string path;
    bool        ok = false;
    ScratchFile(const uint8_t* data, size_t size, const char* tag)
    {
        const char* dir = getenv("TMPDIR");
        if (!dir || !*dir)
            dir = "/tmp";
        std::string       tmpl = std::string(dir) + "/" + tag + "XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int fd = mkstemp(buf.data());
        if (fd < 0)
            return;
        path = buf.data();
        size_t off = 0;
        while (off < size)
        {
            ssize_t w = write(fd, data + off, size - off);
            if (w <= 0)
                break;
            off += size_t(w);
        }
        close(fd);
        ok = off == size;
    }
    ~ScratchFile()
    {
        if (!path.empty())
            unlink(path.c_str());
    }
};
}
