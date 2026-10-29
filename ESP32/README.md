# PBC-gAPI para ESP32-WROOM-32

Copyright © Pablo B. C. Todos os direitos reservados. Esta implementação deriva de
`PBC_GAPI_CUDA/terminalPBC_API.cpp` e `kernel.cu`, no commit `a6b45e7`.
A autoria e a autoridade de copyright permanecem de Pablo B. C., conforme o README
original. Os arquivos das versões anteriores foram preservados.

## O que foi adaptado

- `PBC_gAPI_ESP32/src/PBCgAPI.h`: rasterizador C++11 para CPU, sem dependência de
  CUDA, Windows ou Arduino. Preserva os conceitos `Color` (ordem a,r,g,b),
  `VaryData` com oito floats, `Framebuffer`, `ShaderTri` e shader de fragmento
  fornecido pelo chamador. A interface nova não é uma substituição binária da antiga.
- `PBCMeshes.h`: cubo com normais por face, esfera com normais radiais por vértice
  e gerador da textura quadriculada.
- `examples/CheckerCubeSphere`: exemplo Arduino, configuração de tela e adaptadores
  para **LovyanGFX** e **TFT_eSPI**, entendida aqui como a “ESP-SPI” mencionada no pedido.
- `platformio.ini`: compilação reproduzível para `esp32dev`, com plataforma
  espressif32 6.9.0 / Arduino-ESP32 2.0.17; dependências fixadas nas tags LovyanGFX
  1.2.7 e TFT_eSPI v2.5.33. Não é necessário copiar bibliotecas de terceiros para
  o repositório nem modificar as instalações com PlatformIO.
- `tests`: testes da matemática/rasterização e geração de referências visuais.

O rasterizador usa amostragem no centro do pixel, arestas com quatro bits
subpixel, regra top-left, profundidade float e recorte homogêneo dos seis planos
antes da divisão por w. Funciona com as duas ordens de vértices; o exemplo ativa
descarte de faces traseiras com frente CCW no espaço NDC. Cor e profundidade são
atualizadas sequencialmente na CPU: não utiliza atomics nem a GPU do ESP32.

## Comportamento do exemplo

1. Gera em RAM **64 pixels uint16_t RGB565**, 8×8, alternando `0xFFFF` e `0x0000`
   (128 bytes). Cada quadrado ocupa um texel; amostragem nearest, com repetição.
2. Exibe fundo branco e um cubo girando com normais separadas por face, UVs por
   face e iluminação Blinn-Phong calculada por fragmento.
3. Um toque ou o comando exato `change` na Serial alterna entre cubo e esfera.
   Aceita LF, CRLF, CR ou ausência de terminador (após 120 ms sem receber bytes).
   Toque mantido produz uma troca; é preciso soltar e tocar de novo para outra.
4. A esfera tem **16 setores longitudinais e 16 intervalos de latitude** entre os
   polos: 289 vértices com duplicação de UV na costura/polos e **480 triângulos
   não degenerados**. São 512 triângulos da grade menos os 32 degenerados nos polos.
   As normais são radiais e interpoladas por fragmento, depois normalizadas.
5. O cubo tem velocidade angular +1 rad/s em Y; a esfera, −1 rad/s. A troca
   mantém o ângulo e inverte a velocidade, com uma inclinação fixa em X.
6. A luz direcional branca tem intensidade **1**, direção normalizada
   `(-0.4, 0.6, 1)` apontando da superfície para a fonte, em espaço de câmera;
   a iluminação global/ambiente tem intensidade **0,5**. O material usa componente
   especular branca 0,30 e expoente 32. Resultado RGB limitado a [0,1] na conversão
   RGB565; não há correção gama, HDR ou transparência.
7. A cada dois segundos, informa FPS, tempo médio de CPU, transferência TFT, heap
   livre e maior bloco livre. **Não há promessa de FPS sem medição no hardware.**

Eventos recebidos entre faixas são enfileirados e aplicados no próximo quadro;
um quadro usa a mesma malha e transformação do começo ao fim. Toques muito mais
curtos que o intervalo de amostragem ainda podem passar despercebidos.

## Compilar e gravar com PlatformIO

Abra a pasta `ESP32` como projeto PlatformIO ou execute nela:

```sh
pio run -e lovyan-ili9341
pio run -e lovyan-ili9341 -t upload
pio device monitor -b 115200
```

Escolha o ambiente correspondente ao controlador e à interface reais:

| Ambiente | Biblioteca | Tela / interface | Toque padrão |
| --- | --- | --- | --- |
| `lovyan-ili9341` | LovyanGFX | ILI9341 SPI, 320×240 em paisagem | XPT2046 SPI |
| `espi-ili9341` | TFT_eSPI | ILI9341 SPI, 320×240 em paisagem | XPT2046 SPI |
| `lovyan-parallel-ili9341` | LovyanGFX | ILI9341, paralelo de 8 bits | Desativado |
| `espi-parallel-ili9341` | TFT_eSPI | ILI9341, paralelo de 8 bits | Desativado |
| `lovyan-st7789` / `espi-st7789` | Respectivas | ST7789 SPI, 240×240 | Desativado |
| `lovyan-ili9488` / `espi-ili9488` | Respectivas | ILI9488 SPI, 480×320 em paisagem | Desativado |
| `lovyan-parallel-touch` / `espi-parallel-touch` | Respectivas | ILI9341 paralelo, após conferir eletrodos | Resistivo direto de 4 fios |
| `lovyan-ili9341-half` / `espi-ili9341-half` | Respectivas | ILI9341 SPI, render 160×120 ampliado | XPT2046 SPI |

Para outros perfis, duplique um ambiente e acrescente `-DPBC_DISPLAY_PROFILE=N`:

| Perfil N | Controlador | Interface | Resolução nativa em retrato |
| --- | --- | --- | --- |
| 1 | ILI9341 | SPI | 240×320 |
| 2 | ILI9341 | Paralelo 8 bits | 240×320 |
| 3 | ST7789 | SPI | 240×240 |
| 4 | ST7789 | SPI | 240×320 |
| 5 | ILI9488 | SPI | 320×480 |
| 6 | ST7735, black tab | SPI | 128×160 |
| 7 | GC9A01 | SPI | 240×240 circular |
| 8 | ILI9486 | Paralelo 8 bits | 320×480 |
| 9 | ILI9488 | Paralelo 8 bits | 320×480 |

Nos ambientes personalizados, desative XPT2046 com `-DPBC_TOUCH_XPT2046=0` se
não houver esse controlador; o comando Serial permanece disponível. Os perfis
8/9 não representam placas Raspberry Pi que convertem SPI em paralelo.
ST7735 de outra “tab”, inversão, ordem RGB/BGR e offsets ST7789 podem exigir
ajustes específicos do módulo. Os perfis configurados não equivalem a ensaios físicos.

## Arduino IDE

1. Copie **a pasta `PBC_gAPI_ESP32`** para a pasta `libraries` do sketchbook.
2. Instale o pacote de placas **esp32 da Espressif** e selecione ESP32 Dev Module.
   A configuração verificada de referência é Arduino-ESP32 2.0.17.
3. Instale **LovyanGFX 1.2.7** pelo gerenciador de bibliotecas.
4. Abra `Arquivo → Exemplos → PBC_gAPI_ESP32 → CheckerCubeSphere`.
5. Edite `DisplayConfig.h` para o perfil e o toque da sua placa. O padrão usa
   LovyanGFX + ILI9341 SPI + XPT2046. Compile/grave e abra Serial a 115200 baud.

Para usar **TFT_eSPI v2.5.33 na Arduino IDE**, sua configuração precisa alcançar
também `TFT_eSPI.cpp`, compilado separadamente. Só definir pinos no `.ino` não basta:

1. Defina `PBC_USE_TFT_ESPI` como 1 no `DisplayConfig.h` do exemplo e configure o perfil.
2. Copie esse arquivo configurado para a raiz da instalação de TFT_eSPI,
   com o nome `PBCDisplaySetup.h`.
3. No início de `TFT_eSPI/User_Setup_Select.h`, antes de `#ifndef USER_SETUP_LOADED`,
   acrescente `#include "PBCDisplaySetup.h"`.
4. Mantenha as duas cópias iguais ao mudar a configuração e recompile.

PlatformIO injeta o mesmo arquivo em **todas** as unidades de compilação com
`-include`; portanto, esses passos adicionais na biblioteca não se aplicam a ele.

## Pinagem SPI — módulos comuns

Estes são **números GPIO do ESP32-WROOM-32**, não números físicos dos terminais
do módulo nem posições de conectores. São os valores do exemplo para um DevKit.
Com SPI, os perfis ILI9341, ST7789, ST7735, ILI9488 e GC9A01 compartilham o mapa:

| Sinal da tela | GPIO ESP32 | Observação |
| --- | --- | --- |
| SCK / SCL / CLK | 18 | Clock SPI; “SCL” aqui não significa I²C |
| MOSI / SDA / SDI / DIN | 23 | Dados enviados ao LCD |
| MISO / SDO | 19 | Opcional no LCD; necessário para XPT2046 |
| CS / LCD_CS | 27 | Seleção do LCD |
| DC / RS / A0 | 26 | Comando/dados |
| RESET / RST / RES | 25 | Reset do LCD |
| GND | GND | Terra comum |
| VCC | Conforme o módulo | Consulte a alimentação e o regulador da placa |
| LED / BL | Conforme o módulo | Alimente o backlight pelo circuito adequado |

Com **ILI9488 e outro dispositivo no mesmo SPI** (por exemplo XPT2046), deixe
o SDO do LCD desconectado: a TFT_eSPI alerta que ele não libera MISO ao desativar
CS. Ligue apenas T_DO ao GPIO19 nesse caso; o exemplo não lê pixels do LCD.

O código não controla backlight; ele deve estar alimentado para a imagem aparecer.
Um GPIO não deve alimentar diretamente uma carga de backlight que exceda sua
capacidade. A lógica do ESP32 é **3,3 V**; não conecte saídas de 5 V aos GPIOs.
Alimentação de 5 V em um shield com regulador não garante que seus sinais sejam
compatíveis com 3,3 V: verifique o circuito de adaptação de níveis.

Para **XPT2046** em módulo SPI com toque resistivo:

| Sinal de toque | GPIO ESP32 |
| --- | --- |
| T_CLK | 18 |
| T_DIN | 23 |
| T_DO | 19 |
| T_CS | 22 |
| T_IRQ | 34 |

GPIO34 é somente entrada e não tem pull-up interno. O IRQ deve ter pull-up
externo a 3,3 V, muitas vezes já presente na placa; se o IRQ não for ligado,
altere `PBC_TOUCH_IRQ` para −1 no LovyanGFX. TFT_eSPI detecta o toque pela pressão
SPI e não usa esse IRQ. A detecção é bruta e dispensa calibração de coordenadas.
Não presuma que uma tela ST7789/GC9A01 tenha toque só por usar a mesma pinagem LCD.

## Pinagem paralela de 8 bits — shields do tipo MCUfriend

Se sua tela tem pinos **LCD_D0…D7, LCD_WR, LCD_RD, LCD_RS**, use a interface
paralela. Não ligue esses sinais como se fossem MOSI/MISO/SCK. O exemplo contém
o seguinte mapa por **jumpers individuais**, que evita GPIOs de strapping e
mantém D0…D7 e WR/DC abaixo de GPIO32 para as restrições da TFT_eSPI:

| Sinal LCD | GPIO ESP32 |
| --- | --- |
| D0 | 13 |
| D1 | 14 |
| D2 | 16 |
| D3 | 17 |
| D4 | 18 |
| D5 | 19 |
| D6 | 21 |
| D7 | 22 |
| WR | 25 |
| RD | 32 |
| RS / DC | 27 |
| CS | 26 |
| RESET | 33 |
| GND | GND |

Não é o mapa de encaixe direto de um shield UNO sobre uma placa ESP32 em
formato UNO. Os exemplos upstream da TFT_eSPI usam outro mapa e descrevem
modificações específicas dessas placas. Não use os dois mapas ao mesmo tempo.
Os pinos 16/17 deste mapa são para WROOM-32, não para módulos WROVER com PSRAM.

O toque resistivo de quatro fios de alguns shields compartilha pinos com o LCD.
O exemplo oferece leitura de pressão, mas deve ser habilitada somente depois
de conferir os terminais reais. Para o shield comum XP=LCD_D0, YM=LCD_D1,
XM=LCD_RS, YP=LCD_CS, o mapa resulta em:

| Eletrodo de toque | GPIO ESP32 |
| --- | --- |
| XP | 13 |
| YM | 14 |
| XM | 27 |
| YP | 26 |

Use `-DPBC_TOUCH_4WIRE=1 -DPBC_TOUCH_XPT2046=0` no ambiente paralelo, ou altere
os mesmos valores no `DisplayConfig.h` com Arduino IDE. A leitura ocorre fora
das transações LCD e restaura os pinos compartilhados. GPIO14/27 usam ADC2:
**este caminho presume Wi-Fi desativado**, como no exemplo. O limiar de pressão
pode exigir ajuste. Outras placas mudam os eletrodos; não há autodetecção.
XPT2046 SPI e toque direto de quatro fios são caminhos distintos.

## Sua tela de 320×240

**Resolução e o fato de funcionar com MCUfriend não identificam o controlador.**
Um shield visualmente idêntico pode trazer ILI9341, ILI9325, HX8347 ou outro chip.
Compatibilidade com uma biblioteca Adafruit específica também não determina
a interface nem comprova defeito do módulo.

- Se o controlador for ILI9341 **SPI**, use o perfil 1 e a tabela SPI.
- Se for ILI9341 **paralelo**, use o perfil 2 e a tabela de 8 bits.
- Se for outro controlador, confirme o ID com o exemplo `LCD_ID_readreg` ou
  `diagnose_TFT_support` da MCUFRIEND_kbv no equipamento/pinagem onde a tela já
  funciona e confira o driver correspondente antes de escolher o perfil.
  Não force ILI9341 apenas porque a tela é 240×320 em retrato.
- ILI9325/HX8347 e outros controladores antigos **não são atendidos pelos perfis
  incluídos**. Nesse caso será preciso acrescentar o driver correto ou um terceiro
  adaptador MCUFRIEND compatível com a placa; esta entrega não afirma suporte
  universal a todos os shields MCUfriend.

## RAM, resolução e desempenho

Uma imagem 320×240 em RGB565 ocupa 153.600 bytes. Acrescentar profundidade float
exigiria mais 307.200 bytes: **460.800 bytes apenas para os dois buffers**.
Os 520 KB totais do chip não são todos heap de dados utilizável pelo sketch.

Por isso, o exemplo usa **faixas de 16 linhas**:

| Item | Bytes em 320×240, faixa de 16 linhas |
| --- | --- |
| Cor RGB565: 320×16×2 | 10.240 |
| Profundidade float: 320×16×4 | 20.480 |
| Malhas, índices, cache de vértices e textura | 26.968 |
| Subtotal desses dados | **57.688** |

Há ainda objetos, pilha, buffers dos drivers e memória do framework. As duas
alocações da faixa são verificadas e não requerem PSRAM. Não há alocações de
malha nem de framebuffer dentro do loop. O rasterizador repassa a lista de
triângulos por faixa, rejeitando os que não a interceptam, e transforma os
vértices uma só vez por quadro. A SPI usa 27 MHz por padrão; não se trata de
uma garantia de operação para todos os módulos e comprimentos de fio.

`PBC_RENDER_SCALE=1` renderiza na resolução da tela, **320×240 no ILI9341 em
paisagem**. Com `-DPBC_RENDER_SCALE=2`, renderiza 160×120 e duplica os pixels
na saída 320×240. Essa opção reduz qualidade e custo de fragmentos; não elimina
o envio do quadro inteiro ao LCD. `PBC_STRIP_ROWS=8` reduz o consumo das faixas
pela metade, com mais passagens sobre os triângulos. A transferência é bloqueante,
sem sobrepor DMA e renderização; tearing ainda é possível, pois não há VSYNC/TE.
ILI9488 SPI pode transmitir em 18 bits no barramento, mesmo com entrada RGB565,
conforme o driver, e telas maiores aumentam o custo de CPU e transferência.

## Shaders programáveis e correção de perspectiva

Edite `ExampleVertexShader` e `ExampleFragmentShader` no `.ino`. São funções C++
executadas na CPU, não GLSL nem shaders de uma GPU. Não é necessário alterar
o rasterizador para substituir esses dois estágios.

O vertex shader devolve `VertexOut {clip, vary}`, com `clip` homogêneo e oito
varyings de usuário. O shader de exemplo usa:

| Índices `vary.v` | Conteúdo |
| --- | --- |
| 0…2 | Normal no espaço de câmera |
| 3…5 | Posição no espaço de câmera |
| 6…7 | Coordenadas UV |

Todos os oito valores usam `(Σ λᵢ·aᵢ/wᵢ)/(Σ λᵢ/wᵢ)` durante a rasterização.
A normal interpolada é normalizada no fragment shader antes do cálculo
Blinn-Phong; o vetor de visão vem da posição interpolada. A profundidade NDC
usa interpolação afim na tela, que é o tratamento correto nesse espaço.
`TransformShader` aceita transformações rígidas e escala uniforme; para escala
não uniforme, forneça no vertex shader a matriz normal inversa transposta.

O núcleo não inclui carregador OBJ, sombras, blending, MSAA, mipmaps ou filtragem
bilinear. Essas funções não eram necessárias ao exemplo procedural pedido.

## Verificação

```sh
bash tests/run.sh
```

Os testes usam GCC, AddressSanitizer e UndefinedBehaviorSanitizer e verificam:
textura RGB565; quantidade, costura, normais e orientação das malhas; cobertura
top-left sem buracos/duplicatas; duas orientações e culling; valor analítico da
interpolação com w distintos; profundidade; clipping inclusive próximo do olho;
limites de buffer; entradas inválidas; iluminação 1/0,5; e igualdade pixel a pixel
entre a imagem completa e faixas de 16 e 7 linhas. Geram `cube.ppm` e `sphere.ppm`
em `tests/build` como referências da CPU, não capturas do ESP32.

O workflow `esp32-port.yml` executa esses testes e compila os ambientes declarados.
A compilação confirma integração de APIs e orçamento estático de memória;
não verifica sinais elétricos, inicialização de um módulo específico, cores,
toque, FPS real ou tearing. Esses pontos precisam do ESP32 e da tela física.

## Fontes de hardware e bibliotecas

- [Datasheet ESP32-WROOM-32 — Espressif](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)
- [LovyanGFX 1.2.7 e exemplos de configuração](https://github.com/lovyan03/LovyanGFX/tree/1.2.7/examples/HowToUse)
- [TFT_eSPI v2.5.33](https://github.com/Bodmer/TFT_eSPI/tree/v2.5.33)
- [Configuração upstream ILI9341 paralelo](https://github.com/Bodmer/TFT_eSPI/blob/v2.5.33/User_Setups/Setup14_ILI9341_Parallel.h)
- [MCUFRIEND_kbv e exemplos de identificação](https://github.com/prenticedavid/MCUFRIEND_kbv)

As bibliotecas de terceiros mantêm suas próprias licenças e autoria. A biblioteca
PBC-gAPI para ESP32 está incluída em código-fonte neste repositório; dependências
de display são obtidas pelo PlatformIO/gerenciador Arduino.
