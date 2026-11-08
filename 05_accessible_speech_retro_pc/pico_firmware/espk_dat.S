    .section .rodata
    .global espk_dat_blob
    .global espk_dat_end
    .balign 16
espk_dat_blob:
    .incbin ESPK_DAT_FILE
espk_dat_end:
