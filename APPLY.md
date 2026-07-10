# Como aplicar este patch no seu repositório local

Este zip contém só os arquivos novos/alterados (não o repositório inteiro),
para você copiar por cima do seu clone local de
`microslate-firmware-US-International`.

## 1. Extrair e copiar os arquivos

```bash
cd ~/caminho/para/microslate-firmware-US-International

# Ajuste o caminho do zip conforme onde você baixou/salvou
unzip -o ~/Downloads/x3-port-patch.zip -d /tmp/x3-port-patch

# Copia por cima da árvore do repositório (preserva estrutura de pastas)
cp -r /tmp/x3-port-patch/. .
```

Isso vai sobrescrever: `platformio.ini`, `src/main.cpp`,
`lib/GfxRenderer/{GfxRenderer.h,GfxRenderer.cpp}`,
`lib/hal/{HalDisplay.h,HalDisplay.cpp,HalGPIO.h,HalGPIO.cpp}`,
`.github/workflows/{build.yml,release.yml,build-crossink.yml}`, e vai
adicionar `sdkconfig.xteink_x3_x4` e `README-X3-PORT.md`.

## 2. Remover as libs antigas (substituídas pela FreeInk SDK)

```bash
rm -rf lib/EInkDisplay lib/BatteryMonitor lib/InputManager lib/SDCardManager
git add -A lib/EInkDisplay lib/BatteryMonitor lib/InputManager lib/SDCardManager
```

## 3. Renomear o sdkconfig antigo (se ainda existir)

O patch já inclui `sdkconfig.xteink_x3_x4` novo. Remova o antigo
`sdkconfig.xteink_x4` se ele ainda estiver no repositório:

```bash
git rm -f sdkconfig.xteink_x4 2>/dev/null || rm -f sdkconfig.xteink_x4
```

## 4. Adicionar a FreeInk SDK como submódulo git

O `platformio.ini` espera encontrar `freeink-sdk/` ao lado dele:

```bash
git submodule add https://github.com/Free-Ink/freeink-sdk.git freeink-sdk
git submodule update --init --recursive
```

## 5. Revisar e commitar

```bash
git status
git diff --stat

git add -A
git commit -m "Add Xteink X3 support via FreeInk SDK

- HalGPIO: runtime X3/X4 detection via freeink::selectXteinkDevice(),
  device-aware battery and USB-detect paths
- HalDisplay: select X3 panel driver before init, expose runtime
  display geometry getters
- GfxRenderer: switch BW buffer chunking and all coordinate/buffer-size
  math from compile-time X4 constants to runtime panel geometry
- platformio.ini: point EInkDisplay/BatteryMonitor/InputManager/
  SDCardManager at the FreeInk SDK; rename env to xteink_x3_x4
- CI: fetch freeink-sdk submodule, update build artifact paths"

git push
```

## 6. Build

```bash
pio run -e xteink_x3_x4
# ou, para flashar direto:
pio run -e xteink_x3_x4 --target upload --upload-port /dev/ttyUSB0
```

## Leia antes de flashar no X3 de verdade

`README-X3-PORT.md` (incluído neste patch) documenta exatamente o que foi
alterado e — mais importante — **dois pontos que não puderam ser validados
sem hardware real**: a detecção de USB/carregamento no X3 (via battery gauge,
já que não sobra pino de GPIO dedicado) e um gap pequeno no refresh
assíncrono (`turnOffScreen` não é aplicado nos refreshes não-bloqueantes).
Nenhum dos dois deve impedir o firmware de funcionar, mas vale testar esses
dois comportamentos especificamente no X3 antes de confiar no build no dia a
dia.
