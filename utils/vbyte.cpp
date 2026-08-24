// utils/vbyte.cpp
// Implementation of Variable-Byte integer compression and delta encoding.

#include "vbyte.h"

size_t VByte::encodeUint32(uint32_t val, uint8_t* out) {
    size_t bytes = 0;
    while (val >= 0x80) {
        out[bytes++] = static_cast<uint8_t>((val & 0x7F) | 0x80);
        val >>= 7;
    }
    out[bytes++] = static_cast<uint8_t>(val & 0x7F);
    return bytes;
}

size_t VByte::decodeUint32(const uint8_t* in, uint32_t& val) {
    val = 0;
    size_t shift = 0;
    size_t bytes = 0;
    while (true) {
        uint8_t byte = in[bytes++];
        val |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
    }
    return bytes;
}

std::vector<uint8_t> VByte::encode(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> out;
    out.reserve(values.size() * 2); // typical ~2 bytes per int
    uint8_t buf[5];
    for (uint32_t val : values) {
        size_t len = encodeUint32(val, buf);
        out.insert(out.end(), buf, buf + len);
    }
    return out;
}

std::vector<uint32_t> VByte::decode(const uint8_t* data, size_t size) {
    std::vector<uint32_t> out;
    size_t offset = 0;
    while (offset < size) {
        uint32_t val = 0;
        size_t read = decodeUint32(data + offset, val);
        out.push_back(val);
        offset += read;
    }
    return out;
}

std::vector<uint8_t> VByte::encodeDelta(const std::vector<uint32_t>& sortedValues) {
    if (sortedValues.empty()) return {};

    std::vector<uint32_t> deltas;
    deltas.reserve(sortedValues.size());
    uint32_t prev = 0;
    for (uint32_t val : sortedValues) {
        deltas.push_back(val - prev);
        prev = val;
    }
    return encode(deltas);
}

std::vector<uint32_t> VByte::decodeDelta(const uint8_t* data, size_t size) {
    std::vector<uint32_t> deltas = decode(data, size);
    std::vector<uint32_t> out;
    out.reserve(deltas.size());
    uint32_t running = 0;
    for (uint32_t d : deltas) {
        running += d;
        out.push_back(running);
    }
    return out;
}
