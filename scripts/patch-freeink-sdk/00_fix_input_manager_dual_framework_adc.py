"""Patch em freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:
trocar analogRead()/pinMode() (Arduino) pela API direta do ESP-IDF
(adc1_get_raw/adc1_config_channel_atten) na leitura dos botões via escada
analógica (X3/X4 e qualquer outra placa que use InputStyle::XteinkAdcLadder).

Por quê: o InputManager original do MicroSlate (antes da migração para a
FreeInk SDK) já evitava analogRead() nesse ponto de propósito, com o
comentário:

    "Configure ADC using ESP-IDF API directly to avoid GPIO reconfiguration
    that Arduino's analogRead() triggers on every call in the dual framework"

O MicroSlate compila com `framework = arduino, espidf` (os dois juntos) —
exatamente o cenário que esse comentário avisa ser problemático. A versão da
InputManager vinda da FreeInk SDK usa analogRead() normal, sem essa proteção,
e isso volta a acontecer: reconfiguração de GPIO a cada chamada de
analogRead() (chamada em todo loop() para ler os botões) deixa a leitura
instável o bastante para classificar errado — visto na prática como um
"Right" preso (o cursor avança sozinho e sem parar no editor, que tem
repetição automática de tecla segurada) e como Back/Confirm/Left nunca
registrando enquanto a leitura da escada fica presa na faixa do Right (todos
compartilham o mesmo pino ADC, e só uma classificação pode "vencer" por
leitura).

O CrossInk/CrossPoint não têm esse problema porque compilam só com
`framework = arduino` (sem espidf), então analogRead() não sofre essa
reconfiguração por chamada.

Esse patch é aplicado a cada checkout do submódulo freeink-sdk (ele não
persiste sozinho — git submodule update sempre volta pro commit fixado do
upstream), então roda como parte do CI e deve ser rodado manualmente também
ao atualizar o submódulo localmente.
"""

path = "freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp"
src = open(path).read()

# 1. Include do driver ESP-IDF de ADC
old_include = '#include "InputManager.h"\n\n#if FREEINK_CAP_TOUCH'
new_include = '#include "InputManager.h"\n\n#include <driver/adc.h>\n\n#if FREEINK_CAP_TOUCH'
assert old_include in src, "InputManager.cpp: bloco de include não encontrado — a FreeInk SDK pode ter mudado"
src = src.replace(old_include, new_include)

# 2. begin(): configurar os canais ADC1 via ESP-IDF direto em vez de pinMode()+analogSetAttenuation()
old_begin = """void InputManager::begin() {
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::XteinkAdcLadder) {
    pinMode(BUTTON_ADC_PIN_1, INPUT);
    pinMode(BUTTON_ADC_PIN_2, INPUT);
    pinMode(BoardConfig::ACTIVE.input.power,
            BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    analogSetAttenuation(ADC_11db);
    beginTouch();
    return;
  }"""
new_begin = """void InputManager::begin() {
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::XteinkAdcLadder) {
    // ESP-IDF direto aqui, não pinMode()+analogRead() — ver nota no topo do
    // arquivo de patch (scripts/patch-freeink-sdk/) sobre reconfiguração de
    // GPIO por chamada do Arduino em builds com framework duplo.
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_1), ADC_ATTEN_DB_12);
    adc1_config_channel_atten(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_2), ADC_ATTEN_DB_12);
    pinMode(BoardConfig::ACTIVE.input.power,
            BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    beginTouch();
    return;
  }"""
assert old_begin in src, "InputManager.cpp: InputManager::begin() não bateu — a FreeInk SDK pode ter mudado"
src = src.replace(old_begin, new_begin)

# 3. readButtonAdc(): leitura diagnóstica, mesma troca
old_read_adc = """  group1.raw = analogRead(BUTTON_ADC_PIN_1);
  group1.button = getButtonFromADC(group1.raw, ADC_RANGES_1, NUM_BUTTONS_1);

  group2.raw = analogRead(BUTTON_ADC_PIN_2);
  const int b2 = getButtonFromADC(group2.raw, ADC_RANGES_2, NUM_BUTTONS_2);"""
new_read_adc = """  group1.raw = adc1_get_raw(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_1));
  group1.button = getButtonFromADC(group1.raw, ADC_RANGES_1, NUM_BUTTONS_1);

  group2.raw = adc1_get_raw(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_2));
  const int b2 = getButtonFromADC(group2.raw, ADC_RANGES_2, NUM_BUTTONS_2);"""
assert old_read_adc in src, "InputManager.cpp: readButtonAdc() não bateu — a FreeInk SDK pode ter mudado"
src = src.replace(old_read_adc, new_read_adc)

# 4. getState(): o caminho real usado a cada loop() — o mais importante dos quatro
old_get_state = """  // Read GPIO1 buttons
  const int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  const int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);"""
new_get_state = """  // Read GPIO1 buttons
  const int adcValue1 = adc1_get_raw(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_1));
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  const int adcValue2 = adc1_get_raw(static_cast<adc1_channel_t>(BUTTON_ADC_PIN_2));
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);"""
assert old_get_state in src, "InputManager.cpp: getState() não bateu — a FreeInk SDK pode ter mudado"
src = src.replace(old_get_state, new_get_state)

open(path, "w").write(src)
print("InputManager.cpp: OK — leitura de botões via ADC direto do ESP-IDF (4 pontos corrigidos)")
