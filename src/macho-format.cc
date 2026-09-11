#include "macho-format.h"

#include <algorithm>
#include <limits>

namespace mold::macho {
namespace {

bool has_range(std::span<const uint8_t> data, uint64_t offset, uint64_t size) {
  return offset <= data.size() && size <= data.size() - offset;
}

uint16_t read16(std::span<const uint8_t> data, uint64_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t read32(std::span<const uint8_t> data, uint64_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t read64(std::span<const uint8_t> data, uint64_t offset) {
  return static_cast<uint64_t>(read32(data, offset)) |
         (static_cast<uint64_t>(read32(data, offset + 4)) << 32);
}

std::string read_name(std::span<const uint8_t> data, uint64_t offset) {
  uint64_t size = 0;
  while (size < 16 && data[offset + size] != 0)
    size++;
  return std::string(reinterpret_cast<const char *>(data.data() + offset), size);
}

ParseResult fail(std::string message) {
  ParseResult result;
  result.error = std::move(message);
  return result;
}

bool is_zerofill(uint32_t flags) {
  uint32_t type = flags & SECTION_TYPE;
  return type == S_ZEROFILL || type == S_GB_ZEROFILL ||
         type == S_THREAD_LOCAL_ZEROFILL;
}

bool checked_table_size(uint32_t count, uint32_t element_size, uint64_t &size) {
  size = static_cast<uint64_t>(count) * element_size;
  return count == 0 || size / count == element_size;
}

} // namespace

ParseResult parse(std::span<const uint8_t> data) {
  constexpr uint64_t header_size = 32;
  constexpr uint64_t segment_command_size = 72;
  constexpr uint64_t section_size = 80;
  constexpr uint64_t symtab_command_size = 24;
  constexpr uint64_t symbol_size = 16;
  constexpr uint64_t relocation_size = 8;

  if (!has_range(data, 0, header_size))
    return fail("file is too small for mach_header_64");

  ParseResult result;
  Header64 &header = result.file.header;
  header.magic = read32(data, 0);
  header.cpu_type = read32(data, 4);
  header.cpu_subtype = read32(data, 8);
  header.file_type = read32(data, 12);
  header.command_count = read32(data, 16);
  header.command_bytes = read32(data, 20);
  header.flags = read32(data, 24);
  header.reserved = read32(data, 28);

  if (header.magic != MH_MAGIC_64)
    return fail("unsupported Mach-O magic; v0 accepts little-endian 64-bit Mach-O only");

  if (!has_range(data, header_size, header.command_bytes))
    return fail("load-command region extends past end of file");

  uint64_t commands_end = header_size + header.command_bytes;
  uint64_t command_offset = header_size;

  for (uint32_t i = 0; i < header.command_count; i++) {
    if (!has_range(data, command_offset, 8) || command_offset + 8 > commands_end)
      return fail("truncated load command");

    uint32_t command_type = read32(data, command_offset);
    uint32_t command_size = read32(data, command_offset + 4);

    if (command_size < 8)
      return fail("load command is smaller than load_command");
    if (command_size % 8 != 0)
      return fail("64-bit load command size is not 8-byte aligned");
    if (command_size > commands_end - command_offset)
      return fail("load command extends past declared load-command region");

    result.file.commands.push_back({command_type, command_size, command_offset});

    if (command_type == LC_SEGMENT_64) {
      if (command_size < segment_command_size)
        return fail("LC_SEGMENT_64 is too small");

      uint32_t section_count = read32(data, command_offset + 64);
      uint64_t sections_bytes = 0;
      if (!checked_table_size(section_count, section_size, sections_bytes) ||
          sections_bytes > command_size - segment_command_size)
        return fail("LC_SEGMENT_64 section table extends past command");

      Segment64 segment;
      segment.name = read_name(data, command_offset + 8);
      segment.vm_address = read64(data, command_offset + 24);
      segment.vm_size = read64(data, command_offset + 32);
      segment.file_offset = read64(data, command_offset + 40);
      segment.file_size = read64(data, command_offset + 48);
      segment.max_protection = read32(data, command_offset + 56);
      segment.initial_protection = read32(data, command_offset + 60);
      segment.flags = read32(data, command_offset + 68);

      if (segment.file_size != 0 &&
          !has_range(data, segment.file_offset, segment.file_size))
        return fail("LC_SEGMENT_64 file range extends past end of file");

      segment.sections.reserve(section_count);
      for (uint32_t j = 0; j < section_count; j++) {
        uint64_t section_offset = command_offset + segment_command_size +
                                  static_cast<uint64_t>(j) * section_size;

        Section64 section;
        section.section_name = read_name(data, section_offset);
        section.segment_name = read_name(data, section_offset + 16);
        section.address = read64(data, section_offset + 32);
        section.size = read64(data, section_offset + 40);
        section.file_offset = read32(data, section_offset + 48);
        section.alignment_power = read32(data, section_offset + 52);
        section.relocation_offset = read32(data, section_offset + 56);
        section.relocation_count = read32(data, section_offset + 60);
        section.flags = read32(data, section_offset + 64);
        section.reserved1 = read32(data, section_offset + 68);
        section.reserved2 = read32(data, section_offset + 72);
        section.reserved3 = read32(data, section_offset + 76);

        if (!is_zerofill(section.flags) && section.size != 0 &&
            !has_range(data, section.file_offset, section.size))
          return fail("section contents extend past end of file");

        uint64_t relocations_bytes = 0;
        if (!checked_table_size(section.relocation_count, relocation_size,
                                relocations_bytes) ||
            !has_range(data, section.relocation_offset, relocations_bytes))
          return fail("section relocation table extends past end of file");

        section.relocations.reserve(section.relocation_count);
        for (uint32_t k = 0; k < section.relocation_count; k++) {
          uint64_t relocation_offset = section.relocation_offset +
                                       static_cast<uint64_t>(k) * relocation_size;
          uint32_t address = read32(data, relocation_offset);
          uint32_t info = read32(data, relocation_offset + 4);

          Relocation relocation;
          relocation.offset = static_cast<int32_t>(address);
          relocation.index = info & 0x00ffffff;
          relocation.pc_relative = (info >> 24) & 1;
          relocation.size_power = (info >> 25) & 3;
          relocation.external = (info >> 27) & 1;
          relocation.type = (info >> 28) & 0x0f;
          section.relocations.push_back(relocation);
        }

        segment.sections.push_back(std::move(section));
      }

      result.file.segments.push_back(std::move(segment));
    } else if (command_type == LC_SYMTAB) {
      if (command_size < symtab_command_size)
        return fail("LC_SYMTAB is too small");
      if (result.file.symbol_table.has_value())
        return fail("multiple LC_SYMTAB commands are not supported");

      SymbolTable table;
      table.symbol_offset = read32(data, command_offset + 8);
      uint32_t symbol_count = read32(data, command_offset + 12);
      table.string_offset = read32(data, command_offset + 16);
      table.string_size = read32(data, command_offset + 20);

      uint64_t symbols_bytes = 0;
      if (!checked_table_size(symbol_count, symbol_size, symbols_bytes) ||
          !has_range(data, table.symbol_offset, symbols_bytes))
        return fail("symbol table extends past end of file");
      if (!has_range(data, table.string_offset, table.string_size))
        return fail("string table extends past end of file");

      table.symbols.reserve(symbol_count);
      for (uint32_t j = 0; j < symbol_count; j++) {
        uint64_t symbol_offset = table.symbol_offset +
                                 static_cast<uint64_t>(j) * symbol_size;
        Symbol64 symbol;
        symbol.string_offset = read32(data, symbol_offset);
        symbol.type = data[symbol_offset + 4];
        symbol.section_ordinal = data[symbol_offset + 5];
        symbol.descriptor = read16(data, symbol_offset + 6);
        symbol.value = read64(data, symbol_offset + 8);

        if (symbol.string_offset >= table.string_size && symbol.string_offset != 0)
          return fail("symbol name offset extends past string table");

        if (symbol.string_offset != 0) {
          uint64_t name_offset = static_cast<uint64_t>(table.string_offset) +
                                 symbol.string_offset;
          uint64_t strings_end = static_cast<uint64_t>(table.string_offset) +
                                 table.string_size;
          uint64_t end = name_offset;
          while (end < strings_end && data[end] != 0)
            end++;
          if (end == strings_end)
            return fail("symbol name is not NUL-terminated");
          symbol.name.assign(reinterpret_cast<const char *>(data.data() + name_offset),
                             end - name_offset);
        }

        table.symbols.push_back(std::move(symbol));
      }

      result.file.symbol_table = std::move(table);
    }

    command_offset += command_size;
  }

  if (command_offset != commands_end)
    return fail("load-command count does not consume sizeofcmds bytes");

  return result;
}

} // namespace mold::macho
