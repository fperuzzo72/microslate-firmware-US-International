#include "OtaBootSwitch.h"

#include <esp_rom_crc.h>
#include <string.h>

// SPI_FLASH_SEC_SIZE normalmente vem de spi_flash_mmap.h, mas esse header
// não fica visível para libs comuns do PlatformIO quando o projeto usa
// framework = arduino, espidf (só para componentes IDF "oficiais"). O
// tamanho de setor de flash é sempre 0x1000 (4096 bytes) nesse chip, então
// definimos a constante localmente em vez de depender do header.
static constexpr size_t SPI_FLASH_SEC_SIZE = 0x1000;

namespace ota_boot {

uint32_t computeSeqCrc(uint32_t seq) {
  return esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), kOtaSeqCrcLen);
}

bool switchTo(const esp_partition_t* dest) {
  if (!dest) return false;

  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
  return false;
  }
  if (otadata->size < 2 * SPI_FLASH_SEC_SIZE) {
    return false;
  }

  SelectEntry slots[2] = {};
  if (esp_partition_read(otadata, 0, &slots[0], sizeof(SelectEntry)) != ESP_OK ||
      esp_partition_read(otadata, SPI_FLASH_SEC_SIZE, &slots[1], sizeof(SelectEntry)) != ESP_OK) {
    return false;
  }

  // Pick the slot with valid CRC and highest seq, ignoring INVALID/ABORTED.
  int activeIdx = -1;
  uint32_t activeSeq = 0;
  for (int i = 0; i < 2; ++i) {
    if (slots[i].ota_seq == 0xFFFFFFFFu) continue;
    if (slots[i].crc != computeSeqCrc(slots[i].ota_seq)) continue;
    if (slots[i].ota_state == kOtaImgInvalid || slots[i].ota_state == kOtaImgAborted) continue;
    if (activeIdx < 0 || slots[i].ota_seq > activeSeq) {
      activeIdx = i;
      activeSeq = slots[i].ota_seq;
    }
  }

  // ota_seq encoding: (seq - 1) % NUM_OTA_PARTITIONS picks the partition.
  const uint32_t destOtaIdx =
      static_cast<uint32_t>(dest->subtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  if (destOtaIdx > 15) {
    return false;
  }

  // Find smallest seq > activeSeq such that (seq-1) % 2 == destOtaIdx,
  // assuming 2 OTA partitions (matches our partitions.csv with ota_0 + ota_1).
  uint32_t newSeq = activeSeq + 1;
  while (((newSeq - 1u) % 2u) != (destOtaIdx % 2u)) ++newSeq;

  SelectEntry next = {};
  next.ota_seq = newSeq;
  memset(next.seq_label, 0xFF, sizeof(next.seq_label));
  next.ota_state = kOtaImgNew;
  next.crc = computeSeqCrc(next.ota_seq);

  // Write to the OTHER slot (so the bootloader sees a higher seq there).
  const int targetSlot = (activeIdx == 0) ? 1 : 0;
  const size_t targetOff = static_cast<size_t>(targetSlot) * SPI_FLASH_SEC_SIZE;

  if (esp_partition_erase_range(otadata, targetOff, SPI_FLASH_SEC_SIZE) != ESP_OK) {
    return false;
  }
  if (esp_partition_write(otadata, targetOff, &next, sizeof(next)) != ESP_OK) {
    return false;
  }
  return true;
}

}  // namespace ota_boot
