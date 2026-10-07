#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>

const char mon[] PROGMEM = "Pn";
const char tue[] PROGMEM = "Wt";
const char wed[] PROGMEM = "Śr";
const char thu[] PROGMEM = "Cz";
const char fri[] PROGMEM = "Pt";
const char sat[] PROGMEM = "So";
const char sun[] PROGMEM = "Nd";

const char monf[] PROGMEM = "poniedziałek";
const char tuef[] PROGMEM = "wtorek";
const char wedf[] PROGMEM = "środa";
const char thuf[] PROGMEM = "czwartek";
const char frif[] PROGMEM = "piątek";
const char satf[] PROGMEM = "sobota";
const char sunf[] PROGMEM = "niedziela";

const char jan[] PROGMEM = "styczeń";
const char feb[] PROGMEM = "luty";
const char mar[] PROGMEM = "marzec";
const char apr[] PROGMEM = "kwiecień";
const char may[] PROGMEM = "maj";
const char jun[] PROGMEM = "czerwiec";
const char jul[] PROGMEM = "lipiec";
const char aug[] PROGMEM = "sierpień";
const char sep[] PROGMEM = "wrzesień";
const char octt[] PROGMEM = "październik";
const char nov[] PROGMEM = "listopad";
const char decc[] PROGMEM = "grudzień";

const char* const dow[]   PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]  PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[] PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octt, nov, decc };

const char const_PlReady[]    PROGMEM = "[gotowe]";
const char const_PlStopped[]  PROGMEM = "[zatrzymano]";
const char const_PlConnect[]  PROGMEM = "[łączenie]";
const char const_DlgVolume[]  PROGMEM = "GŁOŚNOŚĆ";
const char const_DlgLost[]    PROGMEM = "* BRAK SIECI *";
const char const_DlgUpdate[]  PROGMEM = "* AKTUALIZACJA *";

const char apNameTxt[] PROGMEM = "NAZWA AP";
const char apPassTxt[] PROGMEM = "HASŁO";
const char bootstrFmt[] PROGMEM = "Łączenie z %s";
const char apSettFmt[] PROGMEM = "USTAWIENIA: HTTP://%s/";
#endif
