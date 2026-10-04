# Hardware targets i descriptor PCB

Target opisuje konkretne PCB: MCU, przypisane GPIO, magistrale i mo?liwo?ci fizyczne. Rodzina MCU sama w sobie nie identyfikuje PCB. Ustawienia runtime, takie jak wyb?r LCD, BT, preset DSP lub preferencje u?ytkownika, nie tworz? nowego targetu. Nowe PCB dostaje nowy descriptor; inna obsada tego samego PCB nie wymaga nowego targetu, je?li opis pin?w i mo?liwo?ci nadal jest prawdziwy.

Warstwa src/hardware/hardware_descriptor.h rozdziela to?samo?? PCB, magistrale SPI/I2C, linie I2S/UART, piny urz?dze? (LCD, enkoder, DAC) oraz mo?liwo?ci. kNoPin reprezentuje brak przypisanego GPIO; adapter fromLegacyPin t?umaczy dotychczasowe 255 i -1. Zero w polu rozmiaru pami?ci oznacza brak potwierdzonej warto?ci. Runtime odczytuje descriptor w dac_mute, rtcsupport, bt_link i bt_audio_input; pozosta?e modu?y nadal korzystaj? z makr profiles/ i Config.

## Obecne targety migracyjne

| Target | MCU | Potwierdzone elementy |
|---|---|---|
| DESK | ESP32 | ST7789 284?76, enkoder, PCM5102A; brak BT |
| DIN | ESP32-S3 | bez LCD i enkodera, PCM5102A, VoxOneBT UART; piny BT I2S s? zarezerwowane, lecz RX jest wy??czony |
| SALON | ESP32-S3 | ST7796S 480?320, enkoder, PCM5102A, DS3231 na I2C, VoxOneBT UART i BT I2S RX |

Descriptor bierze znane piny z aktywnego profilu. Niepotwierdzone piny domy?lnego SPI DESK pozostaj? kNoPin; znane SPI SALON (SCK 12, MOSI 11, MISO 13) opisuje magistral? tylko raz. DS3231 i przysz?e urz?dzenie I2C mog? wsp??dzieli? jedn? par? SDA/SCL bez powielania pin?w w descriptorze. Pin XSMT SALON nadal jest nieprzypisany. Mo?liwo?? fizycznego DSP i MAX98357 dla obecnych trzech PCB pozostaje wy??czona. Stary profil SALON_DSP nie ma kompletnej mapy i nadal nie jest targetem builda.

## Docelowy plan

| Planowany target | MCU | Docelowa obsada audio |
|---|---|---|
| N16R8_MAIN | ESP32-S3 N16R8 | PCM5102A albo DSPmini na jednym PCB |
| S3_ZERO_DIN | ESP32-S3 Zero 4 MB flash / 2 MB PSRAM | PCM5102A |
| S3_ZERO_PORTABLE | ESP32-S3 Zero 4 MB flash / 2 MB PSRAM | PCM5102A albo MAX98357 na jednym PCB |
| ESP32_LEGACY | ESP32 | PCM5102A; utrzymywany do stable |

Dla trzech nowych PCB nie ma jeszcze potwierdzonych map GPIO, wi?c nie maj? gotowych descriptor?w. Planowane klasy wy?wietlaczy to wsp?lny UI 128?64 dla SSD1306 0,96?, SH1106 1,3? i SSD1309 2,4?, a osobne klasy dla SSD1322 256?64, GC9A01 240?240, ST7789 320?240 i ST7796S 480?320. Obecny ST7789 284?76 pozostaje legacy do stable. Ten etap nie dodaje DisplayManagera, driver?w ani prze??czania wy?wietlacza.
