#pragma once

#include <FS.h>
#include <Print.h>

// Stream to a sibling file, verify the closed file, then atomically replace
// the live LittleFS configuration. Never remove the live file before rename.
// No JSON document or dynamically allocated output buffer is needed here.
class PteroConfigWriter : public Print
{
public:
    PteroConfigWriter(fs::FS &fs, const char *path, const char *temporary)
        : _fs(fs), _path(path), _temporary(temporary), _file(fs.open(temporary, "w")),
          _ok(bool(_file)), _size(0), _hash(2166136261u) {}

    explicit operator bool() const { return _ok; }
    using Print::write;

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t size) override
    {
        if (!_ok) return 0;
        size_t written = _file.write(data, size);
        if (written != size) _ok = false;
        _size += size;
        for (size_t i = 0; i < size; ++i) _hash = (_hash ^ data[i]) * 16777619u;
        return written;
    }

    bool commit()
    {
        // File::flush/close do not expose sync errors on ESP8266. Reopen and
        // check length plus content hash instead of trusting getWriteError().
        _file.flush();
        _file.close();
        bool valid = _ok && _size != 0;
        if (valid) {
            File check = _fs.open(_temporary, "r");
            valid = check && check.size() == _size;
            uint32_t hash = 2166136261u;
            size_t remaining = _size;
            uint8_t buffer[64];
            while (valid && remaining) {
                size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                int read = check.read(buffer, count);
                if (read != (int)count) { valid = false; break; }
                for (size_t i = 0; i < count; ++i) hash = (hash ^ buffer[i]) * 16777619u;
                remaining -= count;
            }
            valid = valid && hash == _hash;
            check.close();
        }
        if (valid && _fs.rename(_temporary, _path)) return true;
        _fs.remove(_temporary);
        return false;
    }

private:
    fs::FS &_fs;
    const char *_path;
    const char *_temporary;
    File _file;
    bool _ok;
    size_t _size;
    uint32_t _hash;
};
