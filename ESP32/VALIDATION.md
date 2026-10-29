# Verificação da versão ESP32

Copyright © Pablo B. C. Todos os direitos reservados.

Base: `a6b45e75d82a22d64cbbf96e09bfa927e0dc5467` do PBC-gAPI.
Verificação realizada em 29/10/2026, sem ESP32 ou tela física conectados.

## Testes executados na CPU

```sh
ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh
```

**PASS**: malhas, textura/RGB565, regra top-left, ordem dos vértices e culling,
interpolação com correção de perspectiva, profundidade, recorte, limites dos
buffers, iluminação e equivalência pixel a pixel entre quadro inteiro e faixas.
Os renders de referência foram inspecionados visualmente.

AddressSanitizer e UndefinedBehaviorSanitizer estavam ativos. Apenas a detecção
de vazamentos do LeakSanitizer foi desativada, pois o ambiente não disponibiliza
o acesso a `/proc` necessário para esse verificador. O script pode ser executado
sem essa variável em um Linux convencional.

## Compilação para o ESP32-WROOM-32

PlatformIO 6.2.0; espressif32 6.9.0; Arduino-ESP32 2.0.17;
Xtensa GCC 8.4.0+2021r2-patch5; placa `esp32dev`; sem PSRAM.

| Ambiente | Resultado |
| --- | --- |
| lovyan-ili9341 | SUCCESS |
| espi-ili9341 | SUCCESS |
| lovyan-parallel-ili9341 | SUCCESS |
| espi-parallel-ili9341 | SUCCESS |
| lovyan-st7789 | SUCCESS |
| espi-st7789 | SUCCESS |
| lovyan-ili9488 | SUCCESS |
| espi-ili9488 | SUCCESS |
| lovyan-parallel-touch | SUCCESS |
| espi-parallel-touch | SUCCESS |
| lovyan-ili9341-half | SUCCESS |
| espi-ili9341-half | SUCCESS |

Resumo final: **12 succeeded**. Cada ambiente gerou `firmware.elf` e
`firmware.bin`. Nos perfis sem XPT2046, a TFT_eSPI emite seu aviso esperado de
que a extensão de toque SPI está desativada; o toque direto de quatro fios
usa a leitura implementada no adaptador do exemplo.

O ambiente padrão LovyanGFX/ILI9341 reportou 50.404 bytes de RAM estática e
357.397 bytes de flash. TFT_eSPI/ILI9341 reportou 48.904 bytes de RAM estática
e 303.613 bytes de flash. Esses valores **não incluem** os 30.720 bytes das
faixas alocados no heap em 320×240, nem demais alocações de execução dos drivers.

Os perfis adicionais 4, 6, 7, 8 e 9 de `DisplayConfig.h` são configurações
fornecidas para outros módulos; não fazem parte desses doze ambientes compilados.

## O que ainda depende do hardware

- Identificar controlador, barramento e alimentação da tela específica do usuário.
- Conferir inicialização, orientação, offsets, inversão e ordem RGB/BGR do módulo.
- Conferir SPI/paralelo, alimentação do backlight e integridade elétrica.
- Exercitar `change` pela Serial e os contatos reais de toque com debounce.
- No shield resistivo, confirmar os eletrodos e o limiar de pressão.
- Medir FPS, latência, heap e tearing no ESP32 com a tela ligada.

A compilação e os renders da CPU não comprovam esses itens. O README fornece
a pinagem e o procedimento de seleção de perfil, sem presumir qual chip existe
na tela descrita apenas como “320×240 que funciona com MCUfriend”.

Os fontes CUDA/Win32 e os demais arquivos anteriores permanecem sem alterações;
o README da raiz recebeu somente um link para esta versão adicional.
