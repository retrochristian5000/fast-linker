#!/usr/bin/env bash
. $(dirname $0)/common.inc

SRC=$(dirname "$0")/../src

cat > $t/macho-format-test.cc <<'EOF'
#include "macho-format.h"

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using mold::macho::MachOFile;
using mold::macho::ParseResult;

static void put16(std::vector<uint8_t> &buf, size_t off, uint16_t val) {
  buf[off + 0] = val;
  buf[off + 1] = val >> 8;
}

static void put32(std::vector<uint8_t> &buf, size_t off, uint32_t val) {
  buf[off + 0] = val;
  buf[off + 1] = val >> 8;
  buf[off + 2] = val >> 16;
  buf[off + 3] = val >> 24;
}

static void put64(std::vector<uint8_t> &buf, size_t off, uint64_t val) {
  put32(buf, off, val);
  put32(buf, off + 4, val >> 32);
}

static void put_name(std::vector<uint8_t> &buf, size_t off, const char *name) {
  for (size_t i = 0; name[i] && i < 16; i++)
    buf[off + i] = name[i];
}

static std::vector<uint8_t> make_object() {
  constexpr uint32_t header_size = 32;
  constexpr uint32_t segment_size = 72 + 80;
  constexpr uint32_t symtab_size = 24;
  constexpr uint32_t commands_size = segment_size + symtab_size;
  constexpr uint32_t text_off = header_size + commands_size;
  constexpr uint32_t reloc_off = text_off + 4;
  constexpr uint32_t sym_off = reloc_off + 8;
  constexpr uint32_t str_off = sym_off + 16;

  std::vector<uint8_t> buf(str_off + 7);

  // mach_header_64
  put32(buf, 0, 0xfeedfacf);  // MH_MAGIC_64
  put32(buf, 4, 0x0100000c);  // CPU_TYPE_ARM64
  put32(buf, 8, 0);           // CPU_SUBTYPE_ARM64_ALL
  put32(buf, 12, 1);          // MH_OBJECT
  put32(buf, 16, 2);          // ncmds
  put32(buf, 20, commands_size);
  put32(buf, 24, 0);
  put32(buf, 28, 0);

  // LC_SEGMENT_64 with one section.
  size_t seg = header_size;
  put32(buf, seg + 0, 0x19);
  put32(buf, seg + 4, segment_size);
  put64(buf, seg + 24, 0);
  put64(buf, seg + 32, 4);
  put64(buf, seg + 40, text_off);
  put64(buf, seg + 48, 4);
  put32(buf, seg + 64, 1);

  size_t sec = seg + 72;
  put_name(buf, sec + 0, "__text");
  put_name(buf, sec + 16, "__TEXT");
  put64(buf, sec + 32, 0);
  put64(buf, sec + 40, 4);
  put32(buf, sec + 48, text_off);
  put32(buf, sec + 52, 2);
  put32(buf, sec + 56, reloc_off);
  put32(buf, sec + 60, 1);

  // LC_SYMTAB
  size_t symtab = seg + segment_size;
  put32(buf, symtab + 0, 0x2);
  put32(buf, symtab + 4, symtab_size);
  put32(buf, symtab + 8, sym_off);
  put32(buf, symtab + 12, 1);
  put32(buf, symtab + 16, str_off);
  put32(buf, symtab + 20, 7);

  // __text contents
  put32(buf, text_off, 0xd503201f); // arm64 NOP

  // relocation_info: external, 4-byte width, ARM64_RELOC_UNSIGNED.
  put32(buf, reloc_off + 0, 0);
  put32(buf, reloc_off + 4, (2u << 25) | (1u << 27));

  // nlist_64 for _main: N_SECT | N_EXT, section ordinal 1.
  put32(buf, sym_off + 0, 1);
  buf[sym_off + 4] = 0x0f;
  buf[sym_off + 5] = 1;
  put16(buf, sym_off + 6, 0);
  put64(buf, sym_off + 8, 0);

  buf[str_off] = 0;
  const char name[] = "_main";
  for (size_t i = 0; i < sizeof(name); i++)
    buf[str_off + 1 + i] = name[i];

  return buf;
}

int main() {
  std::vector<uint8_t> buf = make_object();
  ParseResult result = mold::macho::parse(buf);
  assert(result.ok());

  const MachOFile &file = result.file;
  assert(file.header.magic == 0xfeedfacf);
  assert(file.header.cpu_type == 0x0100000c);
  assert(file.header.file_type == 1);
  assert(file.header.command_count == 2);

  assert(file.commands.size() == 2);
  assert(file.segments.size() == 1);
  assert(file.segments[0].sections.size() == 1);

  const auto &section = file.segments[0].sections[0];
  assert(section.section_name == "__text");
  assert(section.segment_name == "__TEXT");
  assert(section.alignment_power == 2);
  assert(section.relocations.size() == 1);
  assert(section.relocations[0].offset == 0);
  assert(section.relocations[0].index == 0);
  assert(section.relocations[0].external);
  assert(!section.relocations[0].pc_relative);
  assert(section.relocations[0].size_power == 2);
  assert(section.relocations[0].type == 0);

  assert(file.symbol_table.has_value());
  assert(file.symbol_table->symbols.size() == 1);
  assert(file.symbol_table->symbols[0].name == "_main");
  assert(file.symbol_table->symbols[0].section_ordinal == 1);
  assert(file.symbol_table->symbols[0].type == 0x0f);

  // A truncated load-command region must be rejected.
  std::vector<uint8_t> truncated(buf.begin(), buf.begin() + 40);
  ParseResult bad = mold::macho::parse(truncated);
  assert(!bad.ok());

  // 64-bit load commands must be aligned to 8 bytes.
  std::vector<uint8_t> misaligned = buf;
  put32(misaligned, 32 + 4, 74);
  bad = mold::macho::parse(misaligned);
  assert(!bad.ok());
}
EOF

$CXX -std=c++20 -Wall -Wextra -Werror -I$SRC \
  $t/macho-format-test.cc $SRC/macho-format.cc -o $t/macho-format-test
$QEMU $t/macho-format-test
