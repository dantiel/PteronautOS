#include "../../lib/WIFI/PteroConfigWriter.h"
#include <cassert>
#include <iostream>

constexpr const char *live = "/pteronautos.conf";
constexpr const char *temp = "/pteronautos.conf.tmp";

int main() {
    const std::string payload = "{\"flight_profiles\":[{\"stroke_ferocity\":21},{\"stroke_ferocity\":42},{\"stroke_ferocity\":63}]}";
    const auto write = [&](fs::FS &fs) {
        PteroConfigWriter writer(fs, live, temp);
        writer.write(reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
        return writer.commit();
    };
    fs::FS good;
    good.files[live] = "old";
    assert(write(good));
    assert(good.files[live] == payload);
    assert(!good.files.count(temp));

    for (int failure = 0; failure < 7; ++failure) {
        fs::FS fs;
        fs.files[live] = "last good configuration";
        fs.openFail = failure == 0;
        fs.shortWrite = failure == 1;
        fs.corruptClose = failure == 2;
        fs.truncateClose = failure == 3;
        fs.readFail = failure == 4;
        fs.reopenFail = failure == 5;
        fs.renameFail = failure == 6;
        assert(!write(fs));
        assert(fs.files[live] == "last good configuration");
    }
    // A power loss before commit leaves only an ignored sibling file.
    fs::FS interrupted;
    interrupted.files[live] = "old";
    interrupted.files[temp] = "incomplete previous write";
    assert(write(interrupted));
    assert(interrupted.files[live] == payload);
    fs::FS firstSave;
    assert(write(firstSave));
    assert(firstSave.files[live] == payload);
    fs::FS empty;
    empty.files[live] = "old";
    PteroConfigWriter writer(empty, live, temp);
    assert(!writer.commit());
    assert(empty.files[live] == "old");
    std::cout << "Profile persistence: success, first save, stale temp, empty write, and 7 injected failures passed\n";
}
