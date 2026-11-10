package com.wul.app.data

import org.json.JSONArray
import org.json.JSONObject

/** Зеркало JSON из API прошивки (firmware/esp32-wul/API.md). Время — локальное «как UTC» (секунды). */
data class Alarm(
  val id: Int = 0,
  val enabled: Boolean = true,
  val hour: Int = 7,
  val minute: Int = 0,
  val days: Int = 0,              // бит0 = Пн … бит6 = Вс; 0 = один раз
  val label: String = "",
  val light: Boolean = true,
  val sunriseMin: Int = 30,       // 20..40, шаг 5
  val intensity: Int = 20,        // 1..20
  val sound: Boolean = true,
  val soundIdx: Int = 0,          // 0..7
  val volume: Int = 10,           // 0..20
  val snoozeMin: Int = 9,         // 1..20
) {
  fun toJson(): JSONObject = JSONObject()
    .put("id", id).put("enabled", enabled).put("hour", hour).put("minute", minute).put("days", days)
    .put("label", label).put("light", light).put("sunriseMin", sunriseMin).put("intensity", intensity)
    .put("sound", sound).put("soundIdx", soundIdx).put("volume", volume).put("snoozeMin", snoozeMin)

  companion object {
    fun from(o: JSONObject) = Alarm(
      o.optInt("id"), o.optBoolean("enabled", true), o.optInt("hour"), o.optInt("minute"), o.optInt("days"),
      o.optString("label", ""), o.optBoolean("light", true), o.optInt("sunriseMin", 30),
      o.optInt("intensity", 20), o.optBoolean("sound", true), o.optInt("soundIdx"), o.optInt("volume", 10),
      o.optInt("snoozeMin", 9),
    )
  }
}

data class Dusk(val minutes: Int = 15, val music: Boolean = true, val musicIdx: Int = 3, val volume: Int = 8)

data class Settings(
  val displayBrightness: Int = 4,
  val trackBedtime: Boolean = true,
  val morningQuestion: Boolean = true,
  val lang: String = "ru",
  val tz: String = DEFAULT_TZ,
  val region: String = "Europe/Kyiv",
  val resetCombo: Boolean = true,
  val soundIdx: Int = 0,          // мелодия для кнопки Звук
  val lightOffAfterAlarm: Int = 0, // погасить свет через N мин после будильника; 0 — оставить
  val boardLed: Boolean = false,   // синий светодиод на плате модуля (по умолчанию выключен — корпус прозрачный)
)

/** Табло часов: часы, надпись из 4 знаков или эквалайзер; secondsLeft = 0 — без ограничения. */
data class DisplayState(val mode: String = "clock", val text: String = "", val secondsLeft: Int = 0)

data class Next(val id: Int, val time: Long)
/** dock: "" — часы ведёт модуль, "device" — в доке iPhone/iPad (часами управляет он), "passive" — модуль отпустил линии вручную */
data class Device(val name: String, val fw: String, val connected: Boolean, val esp: String, val dock: String = "") {
  val docked get() = dock.isNotEmpty()
}

/** Состояние самих часов: лампа, звук кнопки Звук, режим test; pad — к чему сейчас относятся +/− ("light" | "sound"). */
data class ClockState(
  val lightOn: Boolean = false, val lightLevel: Int = 1, val soundOn: Boolean = false, val test: Boolean = false,
  val pad: String = "light",
)

/** Нажатие физической кнопки: id = light|up|down|sound|volUp|volDown|snooze|test. */
data class ButtonEvent(val ms: Long, val id: String, val level: Int)

data class NetInfo(
  val link: String = "wifi",      // как телефон связан с модулем: wifi | ble
  val ssid: String = "", val ip: String = "", val rssi: Int = 0,
  val ap: Boolean = false,        // модуль в режиме точки настройки
  val ble: String = "",
)

/** Режим Bluetooth-колонки (A2DP). bt — модуль в режиме колонки (Wi-Fi выключен, приложение до него не достучится);
 *  showEq/lamp — светомузыка на табло и лампе; name — как модуль виден телефону в списке Bluetooth. */
data class Speaker(
  val available: Boolean = false, val bt: Boolean = false, val showEq: Boolean = true, val lamp: Boolean = true,
  val name: String = "",
  val connected: Boolean = false,  // телефон подключён к колонке
  val playing: Boolean = false,    // идёт музыка
  val peer: String = "",           // имя телефона
  val title: String = "", val artist: String = "",   // что играет (телефон сообщает по AVRCP)
)

/** Звучание динамика часов — эквалайзер модуля (динамик стоит без короба). preset — off|soft|warm|clear|loud|custom;
 *  hp — срез баса, Гц (≤ 50 — без среза); body/presence/treble — плотность 280 Гц, чёткость 3 кГц, верха от 8 кГц, дБ −6..+6.
 *  available = false — прошивка модуля без эквалайзера. Действует на всё: музыку телефона, будильники, закат. */
data class SoundEq(
  val available: Boolean = false, val preset: String = "soft", val on: Boolean = true,
  val hp: Int = 150, val body: Int = 3, val presence: Int = 0, val treble: Int = 0,
)

data class WifiNet(val ssid: String, val rssi: Int, val secure: Boolean)

enum class Mode { Idle, Sunrise, Ringing, Snoozed, Dusk;
  companion object { fun of(s: String) = entries.firstOrNull { it.name.equals(s, true) } ?: Idle }
}

data class WulState(
  val rev: Long,
  val time: Long,                 // 0 = модуль ещё не получил время
  val device: Device,
  val mode: Mode,
  val activeAlarm: Int,
  val snoozeUntil: Long,
  val duskEnd: Long,
  val question: Boolean,
  val volume: Int,
  val next: Next?,
  val alarms: List<Alarm>,
  val dusk: Dusk,
  val settings: Settings,
  val clock: ClockState,
  val buttons: List<ButtonEvent>,
  val net: NetInfo,
  val setup: Boolean,
  val display: DisplayState,
  val speaker: Speaker = Speaker(),
  val eq: SoundEq = SoundEq(),
) {
  companion object {
    fun from(o: JSONObject): WulState {
      val d = o.optJSONObject("device") ?: JSONObject()
      val du = o.optJSONObject("dusk") ?: JSONObject()
      val st = o.optJSONObject("settings") ?: JSONObject()
      val ck = o.optJSONObject("clock") ?: JSONObject()
      val nt = o.optJSONObject("net") ?: JSONObject()
      val dp = o.optJSONObject("display") ?: JSONObject()
      val n = o.optJSONObject("next")
      val sp = o.optJSONObject("speaker")
      return WulState(
        o.getLong("rev"), o.optLong("time"),
        Device(d.optString("name"), d.optString("fw"), d.optBoolean("connected"), d.optString("esp"), d.optString("dock")),
        Mode.of(o.optString("mode")), o.optInt("activeAlarm"), o.optLong("snoozeUntil"), o.optLong("duskEnd"),
        o.optBoolean("question"), o.optInt("volume"),
        n?.let { Next(it.getInt("id"), it.getLong("when")) },
        o.optJSONArray("alarms").objects().map(Alarm::from),
        Dusk(du.optInt("minutes", 15), du.optBoolean("music", true), du.optInt("musicIdx", 3), du.optInt("volume", 8)),
        Settings(
          st.optInt("displayBrightness", 4), st.optBoolean("trackBedtime", true), st.optBoolean("morningQuestion", true),
          st.optString("lang", "ru").ifBlank { "ru" }, st.optString("tz", DEFAULT_TZ).ifBlank { DEFAULT_TZ },
          st.optString("region", "Europe/Kyiv").ifBlank { "Europe/Kyiv" }, st.optBoolean("resetCombo", true), st.optInt("soundIdx"),
          st.optInt("lightOffAfterAlarm"), st.optBoolean("boardLed", false),
        ),
        ClockState(ck.optBoolean("lightOn"), ck.optInt("lightLevel", 1).coerceIn(1, 20), ck.optBoolean("soundOn"), ck.optBoolean("test"),
          ck.optString("pad", "light").ifBlank { "light" }),
        o.optJSONArray("buttons").objects().map { ButtonEvent(it.optLong("ms"), it.optString("id"), it.optInt("level")) },
        NetInfo(nt.optString("link", "wifi"), nt.optString("ssid"), nt.optString("ip"), nt.optInt("rssi"),
          nt.optBoolean("ap"), nt.optString("ble")),
        o.optBoolean("setup"),
        DisplayState(dp.optString("mode", "clock").ifBlank { "clock" }, dp.optString("text"), dp.optInt("secondsLeft")),
        sp?.let { Speaker(it.optBoolean("available"), it.optBoolean("bt"), it.optBoolean("showEq", true), it.optBoolean("lamp", true),
          it.optString("name"), it.optBoolean("connected"), it.optBoolean("playing"), it.optString("peer"),
          it.optString("title"), it.optString("artist")) }
          ?: Speaker(),
        o.optJSONObject("eq")?.let { SoundEq(true, it.optString("preset", "soft").ifBlank { "soft" }, it.optBoolean("on", true),
          it.optInt("hp", 150), it.optInt("body"), it.optInt("presence"), it.optInt("treble")) }
          ?: SoundEq(),
      )
    }
  }
}

data class SleepEntry(val start: Long, val end: Long, val quality: Int, val ease: Int, val snoozes: Int, val note: String) {
  companion object {
    fun from(o: JSONObject) = SleepEntry(o.optLong("start"), o.optLong("end"), o.optInt("quality", -1),
      o.optInt("ease", -1), o.optInt("snoozes"), o.optString("note"))
  }
}

fun JSONArray?.objects(): List<JSONObject> = if (this == null) emptyList() else (0 until length()).map { getJSONObject(it) }

const val DEFAULT_TZ = "EET-2EEST,M3.5.0/3,M10.5.0/4"

/** Регион (IANA) → POSIX TZ для модуля, подписи ru/en. */
data class Region(val id: String, val tz: String, val ru: String, val en: String)

val REGIONS = listOf(
  Region("Europe/Kyiv", DEFAULT_TZ, "Киев (UTC+2/+3)", "Kyiv (UTC+2/+3)"),
  Region("Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3", "Варшава (UTC+1/+2)", "Warsaw (UTC+1/+2)"),
  Region("Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3", "Берлин (UTC+1/+2)", "Berlin (UTC+1/+2)"),
  Region("Europe/London", "GMT0BST,M3.5.0/1,M10.5.0", "Лондон (UTC+0/+1)", "London (UTC+0/+1)"),
  Region("Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4", "Хельсинки (UTC+2/+3)", "Helsinki (UTC+2/+3)"),
  Region("Europe/Chisinau", "EET-2EEST,M3.5.0,M10.5.0/3", "Кишинёв (UTC+2/+3)", "Chisinau (UTC+2/+3)"),
  Region("Europe/Istanbul", "<+03>-3", "Стамбул (UTC+3)", "Istanbul (UTC+3)"),
  Region("Europe/Moscow", "MSK-3", "Москва (UTC+3)", "Moscow (UTC+3)"),
  Region("Asia/Tbilisi", "<+04>-4", "Тбилиси (UTC+4)", "Tbilisi (UTC+4)"),
  Region("Asia/Dubai", "<+04>-4", "Дубай (UTC+4)", "Dubai (UTC+4)"),
  Region("Asia/Almaty", "<+05>-5", "Алматы (UTC+5)", "Almaty (UTC+5)"),
  Region("Asia/Kolkata", "IST-5:30", "Индия (UTC+5:30)", "India (UTC+5:30)"),
  Region("Asia/Shanghai", "CST-8", "Пекин (UTC+8)", "Beijing (UTC+8)"),
  Region("Asia/Tokyo", "JST-9", "Токио (UTC+9)", "Tokyo (UTC+9)"),
  Region("America/New_York", "EST5EDT,M3.2.0,M11.1.0", "Нью-Йорк (UTC−5/−4)", "New York (UTC−5/−4)"),
  Region("America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "Чикаго (UTC−6/−5)", "Chicago (UTC−6/−5)"),
  Region("America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "Лос-Анджелес (UTC−8/−7)", "Los Angeles (UTC−8/−7)"),
  Region("UTC", "UTC0", "UTC", "UTC"),
)
