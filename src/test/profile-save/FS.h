#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
namespace fs { class FS; }
class File {
public:
    File() = default;
    File(fs::FS *fs, const char *path, bool writing);
    explicit operator bool() const { return _fs != nullptr; }
    size_t write(const uint8_t *, size_t);
    int read(uint8_t *, size_t);
    size_t size() const;
    void flush() {}
    void close();
private:
    fs::FS *_fs = nullptr;
    std::string _path;
    bool _writing = false;
    size_t _position = 0;
};
namespace fs {
class FS {
public:
    std::map<std::string, std::string> files;
    bool openFail = false, shortWrite = false, corruptClose = false;
    bool truncateClose = false, readFail = false, reopenFail = false, renameFail = false;
    File open(const char *path, const char *mode) {
        bool writing = mode[0] == 'w';
        if (openFail || (!writing && (reopenFail || !files.count(path)))) return {};
        if (writing) files[path].clear();
        return File(this, path, writing);
    }
    bool rename(const char *from, const char *to) {
        if (renameFail || !files.count(from)) return false;
        files[to] = files[from];
        files.erase(from);
        return true;
    }
    bool remove(const char *path) { return files.erase(path) != 0; }
};
}
inline File::File(fs::FS *fs, const char *path, bool writing)
    : _fs(fs), _path(path), _writing(writing) {}
inline size_t File::write(const uint8_t *data, size_t count) {
    if (!_fs) return 0;
    if (_fs->shortWrite && count) --count;
    _fs->files[_path].append(reinterpret_cast<const char *>(data), count);
    return count;
}
inline size_t File::size() const { return _fs ? _fs->files.at(_path).size() : 0; }
inline int File::read(uint8_t *data, size_t count) {
    if (!_fs || _fs->readFail) return 0;
    const auto &value = _fs->files.at(_path);
    count = std::min(count, value.size() - _position);
    std::memcpy(data, value.data() + _position, count);
    _position += count;
    return static_cast<int>(count);
}
inline void File::close() {
    if (_fs && _writing) {
        auto &value = _fs->files[_path];
        if (_fs->corruptClose && !value.empty()) value[0] ^= 1;
        if (_fs->truncateClose && !value.empty()) value.pop_back();
    }
    _fs = nullptr;
}
