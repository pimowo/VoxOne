#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>

const char mon[] PROGMEM = "pon";
const char tue[] PROGMEM = "wto";
const char wed[] PROGMEM = "śro";
const char thu[] PROGMEM = "czw";
const char fri[] PROGMEM = "pią";
const char sat[] PROGMEM = "sob";
const char sun[] PROGMEM = "nie";

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
const char const_DlgNextion[] PROGMEM = "* NEXTION *";
const char const_waitForSD[]  PROGMEM = "INDEKS SD";

const char apNameTxt[] PROGMEM = "NAZWA AP";
const char apPassTxt[] PROGMEM = "HASŁO";
const char bootstrFmt[] PROGMEM = "Łączenie z %s";
const char apSettFmt[] PROGMEM = "USTAWIENIA: HTTP://%s/";
#endif