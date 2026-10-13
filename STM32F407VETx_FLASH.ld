/*
 * Linker script for CanoKey on STM32F407VET6 (512KB Flash / 192KB RAM)
 *
 * Flash 布局（F407xE 的扇区划分）：
 *   sector 0..3   : 16KB   0x08000000  应用程序起始（前 64KB）
 *   sector 4      : 64KB   0x08010000  应用程序续
 *   sector 5      : 128KB  0x08020000  应用程序续（合计 256KB）
 *   sector 6..7   : 128KB  0x08040000  littlefs 文件系统（2 个 128KB 块）
 *
 * 为什么文件系统放在末尾且只用 2 个块：
 *   F407 的最小擦除单位是扇区，16KB 的扇区只有 sector 0..3 四个，
 *   sector 0 必须留给上电向量表，所以 16KB 块最多只有 3 个。
 *   而 littlefs 中大于 1022 字节的文件必须独占一个块（LFS_ATTR_MAX 限制），
 *   3 个块连 OpenPGP 的初始化都装不下（实测 3/3 写满）。
 *   改用末尾两个 128KB 扇区后，单个元数据块有 128KB 容量，
 *   只要把所有文件都控制在 1022 字节以内即可全部内联，不再需要独立数据块。
 *
 * 复位后 CPU 直接从 0x08000000 取向量，VTOR = 0x08000000，无需 boot stub。
 * 烧录时镜像只覆盖 sector 0..5，不会触碰文件系统区，重复烧固件不会抹掉密钥。
 */

ENTRY(Reset_Handler)

_estack = 0x20020000;          /* SRAM1(112K) + SRAM2(16K) = 128K 末尾 */
_Min_Heap_Size = 0x4000;       /* 16KB heap  */
_Min_Stack_Size = 0x2000;      /*  8KB stack */
_stack_boundary = _estack - _Min_Stack_Size;

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
  LFS   (rx) : ORIGIN = 0x08040000, LENGTH = 256K
  RAM   (xrw): ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
  /* ---- 应用向量表：必须落在 0x08000000 ---- */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* littlefs 起始地址（sector 6, 0x08040000） */
  ._file_system_begin :
  {
    _lfs_begin = .;
  } >LFS

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
