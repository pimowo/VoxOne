/*************************************************************************************
    ST7796 480X320 displays configuration file.
    Copy this file to yoRadio/src/displays/conf/displayST7789conf_custom.h
    and modify it
    More info on https://github.com/e2002/yoradio/wiki/Widgets#widgets-description
*************************************************************************************/

#ifndef displayST7789conf_h
#define displayST7789conf_h

#define DSP_WIDTH       480
#define DSP_HEIGHT      320
#define TFT_FRAMEWDT    10
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2
#define HIDE_VOLBAR

#define bootLogoTop     110

/* SROLLS  */                            /* {{ left, top, fontsize, align }, buffsize, width, scrolldelay, scrolldelta, scrolltime } */
const ScrollConfig metaConf       PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 4, WA_LEFT }, 140, MAX_WIDTH, 5000, 5, 40 };
const ScrollConfig title1Conf     PROGMEM = {{ TFT_FRAMEWDT, 62, 3, WA_LEFT }, 140, MAX_WIDTH, 5000, 5, 40 };
const ScrollConfig title2Conf     PROGMEM = {{ TFT_FRAMEWDT, 102, 3, WA_LEFT }, 140, MAX_WIDTH, 5000, 5, 40 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 146, 3, WA_LEFT }, 140, MAX_WIDTH, 1000, 7, 40 };
const ScrollConfig salonStationConf PROGMEM = {{ 0, 112, 5, WA_CENTER }, 140, DSP_WIDTH, 5000, 5, 40 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 4, WA_CENTER }, 140, MAX_WIDTH, 0, 7, 40 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT, 320-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, MAX_WIDTH, 0, 7, 40 };

/* BACKGROUNDS  */                       /* {{ left, top, fontsize, align }, width, height, outlined } */
const FillConfig   metaBGConf     PROGMEM = {{ 0, 0, 0, WA_LEFT }, DSP_WIDTH, 50, false };
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 50, 0, WA_LEFT }, DSP_WIDTH, 2, false };
const FillConfig  playlBGConf     PROGMEM = {{ 0, 138, 0, WA_LEFT }, DSP_WIDTH, 36, false };

/* WIDGETS  */                           /* { left, top, fontsize, align } */
const WidgetConfig bootstrConf    PROGMEM = { 0, 243, 1, WA_CENTER };
const WidgetConfig bitrateConf    PROGMEM = { 250, 282, 2, WA_LEFT };
const WidgetConfig voltxtConf     PROGMEM = { 10, 293, 2, WA_LEFT };
const WidgetConfig  iptxtConf     PROGMEM = { TFT_FRAMEWDT, 282, 2, WA_LEFT };
const WidgetConfig   rssiConf     PROGMEM = { TFT_FRAMEWDT, 298, 2, WA_RIGHT };
const FillConfig salonPlaybackFrameConf PROGMEM = {{90, 182, 0, WA_LEFT}, 52, 24, true};
const FillConfig salonLoudFrameConf PROGMEM = {{90, 293, 0, WA_LEFT}, 52, 24, true};
const FillConfig salonEqFrameConf PROGMEM = {{156, 293, 0, WA_LEFT}, 72, 24, true};
const FillConfig salonSourceFrameConf PROGMEM = {{242, 293, 0, WA_LEFT}, 100, 24, true};
const FillConfig salonModeFrameConf PROGMEM = {{356, 293, 0, WA_LEFT}, 48, 24, true};
const FillConfig salonLowerDividerConf PROGMEM = {{10, 280, 0, WA_LEFT}, 460, 1, false};
const WidgetConfig salonBluetoothIconConf PROGMEM = { 420, 297, 0, WA_LEFT };
const WidgetConfig btTransportPrevConf PROGMEM = { 80, 176, 8, WA_LEFT };
const WidgetConfig btTransportNextConf PROGMEM = { 80, 176, 8, WA_RIGHT };
const WidgetConfig btTransportPlaybackConf PROGMEM = { 0, 283, 3, WA_CENTER };
const FillConfig btNoteHeadConf PROGMEM = {{ 216, 227, 0, WA_LEFT }, 42, 18, false };
const FillConfig btNoteStemConf PROGMEM = {{ 250, 155, 0, WA_LEFT }, 8, 90, false };
const FillConfig btNoteFlagConf PROGMEM = {{ 250, 155, 0, WA_LEFT }, 29, 8, false };
const FillConfig btNoteFlagEndConf PROGMEM = {{ 271, 161, 0, WA_LEFT }, 8, 32, false };
const WidgetConfig numConf        PROGMEM = { 0, 200, 0, WA_CENTER };
const WidgetConfig salonPlaylistHeaderConf  PROGMEM = { 0, 28, 3, WA_CENTER };
const WidgetConfig salonPlaylistPlayingConf PROGMEM = { 0, 192, 4, WA_CENTER };
const WidgetConfig salonPlaylistCounterConf PROGMEM = { 0, 252, 3, WA_CENTER };
const WidgetConfig apNameConf     PROGMEM = { TFT_FRAMEWDT, 88, 3, WA_CENTER };
const WidgetConfig apName2Conf    PROGMEM = { TFT_FRAMEWDT, 120, 3, WA_CENTER };
const WidgetConfig apPassConf     PROGMEM = { TFT_FRAMEWDT, 173, 3, WA_CENTER };
const WidgetConfig apPass2Conf    PROGMEM = { TFT_FRAMEWDT, 205, 3, WA_CENTER };
const WidgetConfig  clockConf     PROGMEM = { TFT_FRAMEWDT*2, 230, 0, WA_RIGHT };
const WidgetConfig vuConf         PROGMEM = { TFT_FRAMEWDT, 136, 1, WA_LEFT };

const WidgetConfig bootWdtConf    PROGMEM = { 0, 216, 1, WA_CENTER };
const ProgressConfig bootPrgConf  PROGMEM = { 90, 14, 4 };
const BitrateConfig fullbitrateConf PROGMEM = {{90, 220, 2, WA_LEFT}, 44 };
const uint16_t fullbitrateWidth = 52;

/* BANDS  */                             /* { onebandwidth, onebandheight, bandsHspace, bandsVspace, numofbands, fadespeed } */
const VUBandsConfig bandsConf     PROGMEM = { 32, 130, 4, 2, 10, 3 };

/* STRINGS  */
const char         numtxtFmt[]    PROGMEM = "%d";
const char           rssiFmt[]    PROGMEM = "WiFi %d";
const char          iptxtFmt[]    PROGMEM = "IP: %s";
const char        bitrateFmt[]    PROGMEM = "%d kBs";

/* MOVES  */                             /* { left, top, width } */
const MoveConfig    clockMove     PROGMEM = { 0, 176, -1 };

#endif
