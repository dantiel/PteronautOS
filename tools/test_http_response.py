#!/usr/bin/env python3
"""Exercise the actual ESP8266 response pump with a fault-injected TCP client.

No device/network is required. The method is extracted verbatim; the fixture
supplies only Arduino/TCP/source dependencies. Firmware builds check the real
class declarations. Run: python3 tools/test_http_response.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/lib/ESPAsyncWebServer/src/WebResponses.cpp").read_text()
start = source.index("size_t AsyncAbstractResponse::_ackSmall(")
end = source.index("\n#endif", start)
fixture = (root / "tools/http_response_test.cpp").read_text()
basic_start = source.index("size_t AsyncBasicResponse::_ack(")
basic_end = source.index("\n/*\n * Abstract Response", basic_start)
with tempfile.TemporaryDirectory(prefix="ptero-http-") as directory:
    cpp = Path(directory) / "test.cpp"
    cpp.write_text(fixture.replace("// RESPONSE_IMPLEMENTATION", source[start:end])
                   .replace("// BASIC_IMPLEMENTATION", source[basic_start:basic_end]))
    binary = Path(directory) / "test"
    subprocess.run(["clang++", "-std=c++11", "-DESP8266", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
