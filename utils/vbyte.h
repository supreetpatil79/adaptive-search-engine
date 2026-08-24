#ifndef VBYTE_H
#define VBYTE_H

#include <cstdint>
#include <cstddef>
#include <vector>

// Variable-Byte (VByte) integer compression & delta encoding.
// Standard algorithm used in Google, Lucene, and search engines for posting lists.
// Compresses 32-bit integers into 1-5 bytes based on magnitude (7 bits of payload + 1 continuation bit).
//
// Delta encoding transforms monotonic sequences: [100, 105, 120] -> [100, 5, 15]

class VByte {
public:
    // Encode single uint32 into output buffer, returns bytes written
    static size_t encodeUint32(uint32_t val, uint8_t* out);

    // Decode single uint32 from input buffer, returns bytes read
    static size_t decodeUint32(const uint8_t* in, uint32_t& val);

    // Encode a vector of unsigned integers
    static std::vector<uint8_t> encode(const std::vector<uint32_t>& values);

    // Decode a byte array into unsigned integers
    static std::vector<uint32_t> decode(const uint8_t* data, size_t size);

    // Encode sorted integers using delta encoding + VByte
    static std::vector<uint8_t> encodeDelta(const std::vector<uint32_t>& sortedValues);

    // Decode delta-encoded VByte stream back to original sorted integers
    static std::vector<uint32_t> decodeDelta(const uint8_t* data, size_t size);
};

#endif // VBYTE_H
