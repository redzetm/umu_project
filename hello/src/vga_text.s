; VGAテキストバッファに文字列を表示する関数
bits 32
section .text
global draw_ascii

draw_ascii:
  push ebp
  mov  ebp, esp
  
  ; VGAテキストバッファの開始アドレス (0xB8000)
  mov  edi, 0xB8000
  mov  esi, msg
  mov  bl, 0x0F          ; 白文字 on 黒背景

.loop:
  mov  al, [esi]
  cmp  al, 0
  je   .done
  mov  ah, bl
  mov  [edi], ax
  add  esi, 1
  add  edi, 2
  jmp  .loop

.done:
  pop  ebp
  ret

section .rodata
msg: db "Umu Project", 0