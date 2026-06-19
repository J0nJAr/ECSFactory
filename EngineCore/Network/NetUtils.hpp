#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

#include "Math/Fixed64.hpp"

namespace EF::Network {

    class PacketWriter {
    public:
        void WriteUInt8(uint8_t value) {
            m_buffer.push_back(value);
        }

        void U8(uint8_t value) {
            WriteUInt8(value);
        }

        void WriteUInt32(uint32_t value) {
            WriteUInt8(static_cast<uint8_t>((value >> 24) & 0xFF));
            WriteUInt8(static_cast<uint8_t>((value >> 16) & 0xFF));
            WriteUInt8(static_cast<uint8_t>((value >> 8) & 0xFF));
            WriteUInt8(static_cast<uint8_t>(value & 0xFF));
        }

        void U32(uint32_t value) {
            WriteUInt32(value);
        }

        void WriteUInt64(uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                WriteUInt8(static_cast<uint8_t>((value >> shift) & 0xFF));
            }
        }

        void U64(uint64_t value) {
            WriteUInt64(value);
        }

        void WriteInt64(int64_t value) {
            WriteUInt64(static_cast<uint64_t>(value));
        }

        void WriteBool(bool value) {
            WriteUInt8(value ? 1 : 0);
        }

        void Bool(bool value) {
            WriteBool(value);
        }

        void WriteFloat(float value) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            WriteUInt32(bits);
        }

        void Float(float value) {
            WriteFloat(value);
        }

        void WriteFixed64(const Fixed64& value) {
            WriteInt64(value.Raw());
        }

        void WriteBytes(const std::byte* bytes, std::size_t size) {
            const auto* raw = reinterpret_cast<const uint8_t*>(bytes);
            m_buffer.insert(m_buffer.end(), raw, raw + size);
        }

        void Bytes(const std::byte* bytes, std::size_t size) {
            WriteBytes(bytes, size);
        }

        void WriteBytes(std::span<const std::byte> bytes) {
            WriteBytes(bytes.data(), bytes.size());
        }

        template<typename T>
        void WriteTrivial(const T& value) {
            static_assert(std::is_trivially_copyable_v<T>, "WriteTrivial requires a trivially copyable type.");
            WriteBytes(reinterpret_cast<const std::byte*>(&value), sizeof(T));
        }

        [[nodiscard]] const std::vector<uint8_t>& Buffer() const noexcept {
            return m_buffer;
        }

        [[nodiscard]] std::vector<uint8_t> Take() {
            return std::move(m_buffer);
        }

        static void Write(std::vector<uint8_t>& buffer, uint8_t value) {
            buffer.push_back(value);
        }

        static void Write(std::vector<uint8_t>& buffer, uint32_t value) {
            PacketWriter writer;
            writer.WriteUInt32(value);
            const std::vector<uint8_t> written = writer.Take();
            buffer.insert(buffer.end(), written.begin(), written.end());
        }

        static void Write(std::vector<uint8_t>& buffer, uint64_t value) {
            PacketWriter writer;
            writer.WriteUInt64(value);
            const std::vector<uint8_t> written = writer.Take();
            buffer.insert(buffer.end(), written.begin(), written.end());
        }

        static void Write(std::vector<uint8_t>& buffer, const Fixed64& value) {
            Write(buffer, static_cast<uint64_t>(value.Raw()));
        }

        template<typename T>
        static void AutoWrite(std::vector<uint8_t>& buffer, const T& component) {
            static_assert(std::is_trivially_copyable_v<T>, "AutoWrite requires a trivially copyable type.");
            const auto* bytes = reinterpret_cast<const uint8_t*>(&component);
            buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
        }

    private:
        std::vector<uint8_t> m_buffer;
    };

    class PacketReader {
    public:
        explicit PacketReader(std::span<const uint8_t> data)
            : m_data(data) {
        }

        explicit PacketReader(const std::vector<uint8_t>& data)
            : PacketReader(std::span<const uint8_t>(data.data(), data.size())) {
        }

        bool ReadUInt8(uint8_t& value) {
            if (m_offset + 1 > m_data.size()) {
                return false;
            }
            value = m_data[m_offset++];
            return true;
        }

        bool U8(uint8_t& value) {
            return ReadUInt8(value);
        }

        bool ReadUInt32(uint32_t& value) {
            if (m_offset + 4 > m_data.size()) {
                return false;
            }
            value = (static_cast<uint32_t>(m_data[m_offset]) << 24) |
                    (static_cast<uint32_t>(m_data[m_offset + 1]) << 16) |
                    (static_cast<uint32_t>(m_data[m_offset + 2]) << 8) |
                    static_cast<uint32_t>(m_data[m_offset + 3]);
            m_offset += 4;
            return true;
        }

        bool U32(uint32_t& value) {
            return ReadUInt32(value);
        }

        bool ReadUInt64(uint64_t& value) {
            if (m_offset + 8 > m_data.size()) {
                return false;
            }
            value = 0;
            for (int i = 0; i < 8; ++i) {
                value = (value << 8) | static_cast<uint64_t>(m_data[m_offset + i]);
            }
            m_offset += 8;
            return true;
        }

        bool U64(uint64_t& value) {
            return ReadUInt64(value);
        }

        bool ReadInt64(int64_t& value) {
            uint64_t raw = 0;
            if (!ReadUInt64(raw)) {
                return false;
            }
            value = static_cast<int64_t>(raw);
            return true;
        }

        bool ReadBool(bool& value) {
            uint8_t byte = 0;
            if (!ReadUInt8(byte)) {
                return false;
            }
            value = byte != 0;
            return true;
        }

        bool Bool(bool& value) {
            return ReadBool(value);
        }

        bool ReadFloat(float& value) {
            uint32_t bits = 0;
            if (!ReadUInt32(bits)) {
                return false;
            }
            std::memcpy(&value, &bits, sizeof(value));
            return true;
        }

        bool Float(float& value) {
            return ReadFloat(value);
        }

        bool ReadFixed64(Fixed64& value) {
            int64_t raw = 0;
            if (!ReadInt64(raw)) {
                return false;
            }
            value = Fixed64::FromRaw(raw);
            return true;
        }

        bool ReadBytes(std::vector<std::byte>& outBytes, std::size_t size) {
            if (m_offset + size > m_data.size()) {
                return false;
            }
            const auto* begin = reinterpret_cast<const std::byte*>(m_data.data() + m_offset);
            outBytes.assign(begin, begin + size);
            m_offset += size;
            return true;
        }

        bool Bytes(std::vector<std::byte>& outBytes, std::size_t size) {
            return ReadBytes(outBytes, size);
        }

        [[nodiscard]] bool Finished() const noexcept {
            return m_offset == m_data.size();
        }

    private:
        std::span<const uint8_t> m_data;
        std::size_t m_offset = 0;
    };

}
