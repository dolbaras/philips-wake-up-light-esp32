// АВТОГЕНЕРАЦИЯ tools/gen-properties.py — не править вручную.
#pragma once
#include <cstdint>

namespace wul {
enum Cmd : uint8_t {
  CMD_APP_INIT=0x00, CMD_DEV_INIT=0x01, CMD_STEP05=0x05,
  CMD_GET_CAP=0x06, CMD_RET_CAP=0x07, CMD_GET_NAME=0x08, CMD_RET_NAME=0x09,
  CMD_GET_PARAM=0x10, CMD_RET_PARAM=0x11,
  CMD_GET_VALUE=0x12, CMD_RET_VALUE=0x13, CMD_SET_VALUE=0x14
};

static const uint8_t PROP_VOL = 0x00;  // VOL ✓
static const uint8_t PROP_DSC = 0x01;  // DSC
static const uint8_t PROP_DBB = 0x02;  // DBB
static const uint8_t PROP_TREB = 0x03;  // TREB
static const uint8_t PROP_BASS = 0x04;  // BASS
static const uint8_t PROP_BTC = 0x08;  // BTC
static const uint8_t PROP_DATETIME = 0x0B;  // DATETIME
static const uint8_t PROP_ALARM = 0x0C;  // ALARM ✓
static const uint8_t PROP_LOUD = 0x12;  // LOUD
static const uint8_t PROP_CLK = 0x13;  // CLK
static const uint8_t PROP_ALMVOL = 0x14;  // ALMVOL ✓
static const uint8_t PROP_RC = 0x16;  // RC ✓
static const uint8_t PROP_FWVER_MAIN = 0x17;  // FWVER_MAIN ✓
static const uint8_t PROP_FWVER_DOCK = 0x26;  // FWVER_DOCK
static const uint8_t PROP_BLUEBOOST = 0x40;  // BLUEBOOST
static const uint8_t PROP_DUSKMODE = 0x41;  // DUSKMODE
static const uint8_t PROP_DISPBRIGHT = 0x42;  // DISPBRIGHT ✓
static const uint8_t PROP_SNOOZETIME = 0x43;  // SNOOZETIME
static const uint8_t PROP_PWMRED = 0x44;  // PWMRED
static const uint8_t PROP_PWMAMBER = 0x45;  // PWMAMBER
static const uint8_t PROP_PWMWHITE = 0x46;  // PWMWHITE
static const uint8_t PROP_HW_VERLGHTDRV = 0x47;  // HW_VERLGHTDRV
static const uint8_t PROP_FW_VERLGHTDRV = 0x48;  // FW_VERLGHTDRV
static const uint8_t PROP_DUSKPROF = 0x49;  // DUSKPROF ✓
static const uint8_t PROP_PWMBLUE = 0x4A;  // PWMBLUE
static const uint8_t PROP_TIMEREP = 0x4B;  // TIMEREP ✓
static const uint8_t PROP_LIGHTLVL = 0x4C;  // LIGHTLVL ✓
static const uint8_t PROP_LIGHTSENSOR = 0x4D;  // LIGHTSENSOR
static const uint8_t PROP_DAWNPROF = 0x4E;  // DAWNPROF
static const uint8_t PROP_WULCURVECMD = 0x4F;  // WULCURVECMD ✓
static const uint8_t PROP_FMPRESET1 = 0x50;  // FMPRESET1
static const uint8_t PROP_FMPRESET2 = 0x51;  // FMPRESET2
static const uint8_t PROP_FMPRESET3 = 0x52;  // FMPRESET3
static const uint8_t PROP_APPAUTOLAUNCH = 0x53;  // APPAUTOLAUNCH ✓
static const uint8_t PROP_TIMESYNC = 0x54;  // TIMESYNC
static const uint8_t PROP_FMONOFF = 0x55;  // FMONOFF
static const uint8_t PROP_SNOOZESTATUS = 0x56;  // SNOOZESTATUS
static const uint8_t PROP_WULCURVESTATUS = 0x57;  // WULCURVESTATUS ✓
static const uint8_t PROP_STARTAUDIO = 0x58;  // STARTAUDIO ✓
static const uint8_t PROP_NMBR_HRSDEVON = 0x70;  // NMBR_HRSDEVON
static const uint8_t PROP_NMBR_HRSLAMPON = 0x71;  // NMBR_HRSLAMPON
static const uint8_t PROP_NMBR_DAWNCURVES = 0x72;  // NMBR_DAWNCURVES
static const uint8_t PROP_NMBR_DUSKCURVES = 0x73;  // NMBR_DUSKCURVES
static const uint8_t PROP_NMBR_SNOOZES = 0x74;  // NMBR_SNOOZES
static const uint8_t PROP_AVG_NMBRSNOOZES = 0x75;  // AVG_NMBRSNOOZES
static const uint8_t PROP_AVG_MANLIGHTLEVEL = 0x76;  // AVG_MANLIGHTLEVEL
static const uint8_t PROP_AVG_CURVELIGHTLEVEL = 0x77;  // AVG_CURVELIGHTLEVEL
static const uint8_t PROP_AVG_DUSKTIME = 0x78;  // AVG_DUSKTIME
static const uint8_t PROP_AVG_DISPLAYBRIGHTNESS = 0x79;  // AVG_DISPLAYBRIGHTNESS
static const uint8_t PROP_NMBR_WU1 = 0x7A;  // NMBR_WU1
static const uint8_t PROP_NMBR_WU2 = 0x7B;  // NMBR_WU2
static const uint8_t PROP_NMBR_AMPLIFIERON = 0x7C;  // NMBR_AMPLIFIERON
static const uint8_t PROP_NMBR_RADIOON = 0x7D;  // NMBR_RADIOON
static const uint8_t PROP_NMBR_RADIOASWUS = 0x7E;  // NMBR_RADIOASWUS
static const uint8_t PROP_NMBR_IPODASWUS = 0x7F;  // NMBR_IPODASWUS
static const uint8_t PROP_NMBR_SOUND1_ASWUS = 0x80;  // NMBR_SOUND1_ASWUS
static const uint8_t PROP_NMBR_SOUND2_ASWUS = 0x81;  // NMBR_SOUND2_ASWUS
static const uint8_t PROP_NMBR_SOUND3_ASWUS = 0x82;  // NMBR_SOUND3_ASWUS
static const uint8_t PROP_NMBR_SOUND4_ASWUS = 0x83;  // NMBR_SOUND4_ASWUS
static const uint8_t PROP_NMBR_SOUND5_ASWUS = 0x84;  // NMBR_SOUND5_ASWUS
static const uint8_t PROP_NMBR_IPODDOCKS = 0x85;  // NMBR_IPODDOCKS
static const uint8_t PROP_AVG_CURVEDURATION = 0x86;  // AVG_CURVEDURATION
}
