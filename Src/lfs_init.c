// SPDX-License-Identifier: Apache-2.0
/* littlefs 后端：使用内部 flash 的 sector 6~7（每块 128KB，共 2 块）
 *
 * F407 的扇区大小不均匀（16K x4 / 64K / 128K x3），而 littlefs 的块大小必须
 * 等于物理擦除粒度。16KB 扇区最多只能凑出 3 块（sector 0 要给向量表），
 * 实测 3 块连 OpenPGP 的初始化都装不下。
 * 所以改用末尾两个 128KB 扇区，配合"所有文件都小于 1022 字节"的约束
 * （见 rsa.h 的 RSA_N_BIT_MAX 与 openpgp.c 的 MAX_CERT_LENGTH），
 * 让全部文件内容内联进元数据块，从而不需要独立的数据块。
 */
#include "main.h"
#include <fs.h>
#include <memzero.h>
#include <stdalign.h>
#include <string.h>

#define BLOCK_SIZE 131072U
#define BLOCK_COUNT 2U
#define FS_SECTOR_BASE 6U /* sector 6 = 0x08040000，对应链接脚本的 LFS 段 */
#define WRITE_SIZE 4U     /* F4 在 3.3V 下按 32-bit word 编程 */
#define READ_SIZE 1U
#define LOOKAHEAD_SIZE 16U

#define FS_BASE (&_lfs_begin)
#define FLASH_ADDR(b, o) ((uint32_t)FS_BASE + (b)*BLOCK_SIZE + (o))

static struct lfs_config config;
static uint8_t read_buffer[LFS_CACHE_SIZE];
static alignas(4) uint8_t prog_buffer[LFS_CACHE_SIZE];
static alignas(4) uint8_t lookahead_buffer[LOOKAHEAD_SIZE];
extern uint8_t _lfs_begin;

int block_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
  (void)c;
  memcpy(buffer, (const void *)FLASH_ADDR(block, off), size);
  return 0;
}

int block_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
  (void)c;
  if (size % WRITE_SIZE != 0 || off % WRITE_SIZE != 0) return LFS_ERR_INVAL;

  uint32_t paddr = FLASH_ADDR(block, off);
  int ret = 0;

  HAL_FLASH_Unlock();
  for (lfs_size_t i = 0; i < size; i += WRITE_SIZE) {
    uint32_t word;
    memcpy(&word, (const uint8_t *)buffer + i, sizeof(word));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, paddr + i, (uint64_t)word) != HAL_OK) {
      ERR_MSG("Flash prog failed @%#lx\r\n", (unsigned long)(paddr + i));
      ret = LFS_ERR_CORRUPT;
      break;
    }
  }
  HAL_FLASH_Lock();

  return ret;
}

int block_erase(const struct lfs_config *c, lfs_block_t block) {
  (void)c;
  int ret = 0;
  uint32_t SectorError = 0;

  FLASH_EraseInitTypeDef erase = {0};
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = FS_SECTOR_BASE + block;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  HAL_FLASH_Unlock();
  if (HAL_FLASHEx_Erase(&erase, &SectorError) != HAL_OK) {
    ret = LFS_ERR_IO;
    ERR_MSG("HAL_FLASHEx_Erase sector %u failed\r\n", (unsigned)erase.Sector);
  }
  HAL_FLASH_Lock();

  return ret;
}

int block_sync(const struct lfs_config *c) {
  (void)c;
  return 0;
}

void littlefs_init(void) {
  memzero(&config, sizeof(config));
  config.read = block_read;
  config.prog = block_prog;
  config.erase = block_erase;
  config.sync = block_sync;
  config.read_size = READ_SIZE;
  config.prog_size = WRITE_SIZE;
  config.block_size = BLOCK_SIZE;
  config.block_count = BLOCK_COUNT;
  /* 只有 2 个块，元数据对本身就是全部空间。
     块级磨损均衡（block_cycles）到期时会尝试搬迁元数据到新块，
     这里没有空闲块可用，必须关掉——-1 表示禁用。
     元数据仍会在两个块之间交替写入（ping-pong），本身就有磨损均衡效果。 */
  config.block_cycles = -1;
  /* inline_max 会被取 min(cache_size, LFS_ATTR_MAX=1022, block_size/8)，
     所以缓存给到 1024 就够，再大也不会放宽 1022 字节的内联上限。 */
  config.cache_size = LFS_CACHE_SIZE;
  config.lookahead_size = LOOKAHEAD_SIZE;
  config.read_buffer = read_buffer;
  config.prog_buffer = prog_buffer;
  config.lookahead_buffer = lookahead_buffer;
  DBG_MSG("Flash base %p, %u blocks (%u bytes)\r\n", (void *)FS_BASE, (unsigned)BLOCK_COUNT, (unsigned)BLOCK_SIZE);

  int err;
  for (int retry = 0; retry < 3; retry++) {
    err = fs_mount(&config);
    if (!err) return;
  }

  /* 首次上电：格式化 */
  DBG_MSG("Formating data area...\r\n");
  fs_format(&config);
  err = fs_mount(&config);
  if (err) {
    ERR_MSG("Failed to mount FS after formating\r\n");
    for (;;)
      ;
  }
}
