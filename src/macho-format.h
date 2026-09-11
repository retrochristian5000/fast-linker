#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mold::macho {

inline constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
inline constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000c;

inline constexpr uint32_t MH_OBJECT = 0x1;
inline constexpr uint32_t MH_FILESET = 0xc;

inline constexpr uint32_t LC_SYMTAB = 0x2;
inline constexpr uint32_t LC_SEGMENT_64 = 0x19;

inline constexpr uint32_t SECTION_TYPE = 0x000000ff;
inline constexpr uint32_t S_ZEROFILL = 0x1;
inline constexpr uint32_t S_GB_ZEROFILL = 0xc;
inline constexpr uint32_t S_THREAD_LOCAL_ZEROFILL = 0x12;

struct Header64 {
  uint32_t magic = 0;
  uint32_t cpu_type = 0;
  uint32_t cpu_subtype = 0;
  uint32_t file_type = 0;
  uint32_t command_count = 0;
  uint32_t command_bytes = 0;
  uint32_t flags = 0;
  uint32_t reserved = 0;
};

struct LoadCommand {
  uint32_t type = 0;
  uint32_t size = 0;
  uint64_t file_offset = 0;
};

struct Relocation {
  int32_t offset = 0;
  uint32_t index = 0;
  bool pc_relative = false;
  uint8_t size_power = 0;
  bool external = false;
  uint8_t type = 0;
};

struct Section64 {
  std::string section_name;
  std::string segment_name;
  uint64_t address = 0;
  uint64_t size = 0;
  uint32_t file_offset = 0;
  uint32_t alignment_power = 0;
  uint32_t relocation_offset = 0;
  uint32_t relocation_count = 0;
  uint32_t flags = 0;
  uint32_t reserved1 = 0;
  uint32_t reserved2 = 0;
  uint32_t reserved3 = 0;
  std::vector<Relocation> relocations;
};

struct Segment64 {
  std::string name;
  uint64_t vm_address = 0;
  uint64_t vm_size = 0;
  uint64_t file_offset = 0;
  uint64_t file_size = 0;
  uint32_t max_protection = 0;
  uint32_t initial_protection = 0;
  uint32_t flags = 0;
  std::vector<Section64> sections;
};

struct Symbol64 {
  std::string name;
  uint32_t string_offset = 0;
  uint8_t type = 0;
  uint8_t section_ordinal = 0;
  uint16_t descriptor = 0;
  uint64_t value = 0;
};

struct SymbolTable {
  uint32_t symbol_offset = 0;
  uint32_t string_offset = 0;
  uint32_t string_size = 0;
  std::vector<Symbol64> symbols;
};

struct MachOFile {
  Header64 header;
  std::vector<LoadCommand> commands;
  std::vector<Segment64> segments;
  std::optional<SymbolTable> symbol_table;
};

struct ParseResult {
  MachOFile file;
  std::string error;

  bool ok() const { return error.empty(); }
};

ParseResult parse(std::span<const uint8_t> data);

} // namespace mold::macho
