#include <cstdint>
#include <sstream>
#include <string>
#include <cstring>

namespace {

std::string fourccFromData1(uint32_t data1) {
    char s[5] = {
        static_cast<char>(data1 & 0xFF),
        static_cast<char>((data1 >> 8) & 0xFF),
        static_cast<char>((data1 >> 16) & 0xFF),
        static_cast<char>((data1 >> 24) & 0xFF),
        0,
    };
    return std::string(s);
}

std::string fourccFromGuidAscii(const std::string& guid) {
    // First 8 hex chars of GUID = 4 ASCII bytes in display order
    if (guid.size() < 8) return "????";
    char s[5] = {
        static_cast<char>(std::stoi(guid.substr(0, 2), nullptr, 16)),
        static_cast<char>(std::stoi(guid.substr(2, 2), nullptr, 16)),
        static_cast<char>(std::stoi(guid.substr(4, 2), nullptr, 16)),
        static_cast<char>(std::stoi(guid.substr(6, 2), nullptr, 16)),
        0,
    };
    return std::string(s);
}

}  // namespace

std::string analyzePixelFormat(const std::string& identifier) {
    std::ostringstream oss;

    if (identifier.find('-') != std::string::npos) {
        // GUID form: 47524247-0000-1000-8000-00aa00389b71
        const uint32_t data1 = std::stoul(identifier.substr(0, 8), nullptr, 16);
        const std::string leFourcc = fourccFromData1(data1);
        const std::string asciiFourcc = fourccFromGuidAscii(identifier);

        oss << "GUID: " << identifier << "\n";
        oss << "Data1 LE FOURCC: " << leFourcc << " (0x" << std::hex << data1 << std::dec << ")\n";
        oss << "Data1 ASCII (display order): " << asciiFourcc << "\n";
        oss << "Suffix: " << identifier.substr(9) << "\n";
        oss << "Standard DirectShow FOURCC suffix: 0000-0010-8000-00AA00389B71\n";
        oss << "NexImage suffix (non-standard):    0000-1000-8000-00AA00389B71\n\n";

        if (asciiFourcc == "GRBG") {
            oss << "FORMAT CONFIRMÉ: GRBG 8-bit Bayer (GRGR/BGBG)\n";
            oss << "libuvc: UVC_FRAME_FORMAT_SGRBG8 / fourcc \"GRBG\"\n";
            oss << "V4L2: V4L2_PIX_FMT_GRBG\n";
            oss << "Bits/pixel: 8 | Debayer: OUI\n";
            oss << "Frame size 3872x2764: " << (3872 * 2764) << " bytes\n";
        } else {
            oss << "Pattern: " << asciiFourcc << " (verify on device)\n";
        }
    } else if (identifier.size() >= 4) {
        oss << "FOURCC: " << identifier.substr(0, 4) << "\n";
        if (identifier.substr(0, 4) == "Y800") {
            oss << "FORMAT: 8-bit grayscale (MONO8)\n";
            oss << "libuvc: UVC_FRAME_FORMAT_GRAY8\n";
            oss << "Debayer: NON\n";
        } else if (identifier.substr(0, 4) == "GRBG") {
            oss << "FORMAT: 8-bit Bayer GRBG\n";
            oss << "libuvc: UVC_FRAME_FORMAT_SGRBG8\n";
            oss << "Debayer: OUI\n";
        }
    } else {
        oss << "Identifier too short";
    }

    return oss.str();
}
