package com.wul.app

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.wul.app.data.Alarm
import com.wul.app.data.Api
import com.wul.app.data.ApiException
import com.wul.app.data.BleDevice
import com.wul.app.data.BleTransport
import com.wul.app.data.Discovery
import com.wul.app.data.HttpTransport
import com.wul.app.data.SleepEntry
import com.wul.app.data.SppTransport
import com.wul.app.data.connectedSpeaker
import com.wul.app.data.hasSppPermissions
import com.wul.app.data.WifiNet
import com.wul.app.data.WulState
import com.wul.app.data.bleScan
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.util.Locale

enum class ThemePref { Dark, Light, Auto }

/** Способ связи телефона с модулем. */
enum class Link { Wifi, Ble }

/** Связь с модулем: поиск (Wi-Fi/mDNS или Bluetooth), опрос состояния раз в 1.5 с, команды. */
class MainViewModel(app: Application) : AndroidViewModel(app) {
  private val prefs = app.getSharedPreferences("wul", Context.MODE_PRIVATE)

  private val _link = MutableStateFlow(if (prefs.getString("link", "wifi") == "ble") Link.Ble else Link.Wifi)
  val link: StateFlow<Link> = _link.asStateFlow()

  private val _host = MutableStateFlow(prefs.getString("host", null))
  val host: StateFlow<String?> = _host.asStateFlow()

  private val _bleDevice = MutableStateFlow(prefs.getString("bleAddr", null)?.let { BleDevice(prefs.getString("bleName", it)!!, it, 0) })
  val bleDevice: StateFlow<BleDevice?> = _bleDevice.asStateFlow()

  private val _state = MutableStateFlow<WulState?>(null)
  val state: StateFlow<WulState?> = _state.asStateFlow()

  /** true — модуль отвечает (не путать с device.connected: связь модуля с часами). */
  /** Связь идёт по Bluetooth SPP: модуль в режиме колонки (Wi-Fi у него выключен). */
  private val _viaSpeaker = MutableStateFlow(false)
  val viaSpeaker: StateFlow<Boolean> = _viaSpeaker.asStateFlow()

  private val _online = MutableStateFlow(false)
  val online: StateFlow<Boolean> = _online.asStateFlow()

  private val _searching = MutableStateFlow(false)
  val searching: StateFlow<Boolean> = _searching.asStateFlow()

  private val _bleFound = MutableStateFlow<List<BleDevice>>(emptyList())
  val bleFound: StateFlow<List<BleDevice>> = _bleFound.asStateFlow()
  private val _bleScanning = MutableStateFlow(false)
  val bleScanning: StateFlow<Boolean> = _bleScanning.asStateFlow()

  private val _log = MutableStateFlow<List<SleepEntry>>(emptyList())
  val log: StateFlow<List<SleepEntry>> = _log.asStateFlow()

  private val _wifi = MutableStateFlow<List<WifiNet>?>(null)     // null — ещё не сканировали
  val wifi: StateFlow<List<WifiNet>?> = _wifi.asStateFlow()
  private val _wifiScanning = MutableStateFlow(false)
  val wifiScanning: StateFlow<Boolean> = _wifiScanning.asStateFlow()

  private val _toast = MutableStateFlow<String?>(null)
  val toast: StateFlow<String?> = _toast.asStateFlow()

  private val _theme = MutableStateFlow(runCatching { ThemePref.valueOf(prefs.getString("theme", "Dark")!!) }.getOrDefault(ThemePref.Dark))
  val theme: StateFlow<ThemePref> = _theme.asStateFlow()

  /** Язык интерфейса: с модуля, до подключения — последний известный или язык системы. */
  private val _lang = MutableStateFlow(prefs.getString("lang", null) ?: if (Locale.getDefault().language == "ru" || Locale.getDefault().language == "uk") "ru" else "en")
  val lang: StateFlow<String> = _lang.asStateFlow()

  /** Секундный тик для таймеров между опросами (время модуля + прошедшее). */
  private val _clock = MutableStateFlow(0L)
  val clock: StateFlow<Long> = _clock.asStateFlow()

  private var api: Api? = null
  private var pollJob: Job? = null
  private var failures = 0
  private var generation = 0                      // растёт при каждой смене канала — долгий поиск колонки не перебьёт выбор

  init {
    rebuildTransport()
    if (api == null && _link.value == Link.Wifi) discover()
    viewModelScope.launch {                       // часы — Bluetooth-колонка? (см. findSpeakerOnce)
      while (isActive) {
        delay(4000)
        if (!_viaSpeaker.value && (api == null || !_online.value)) findSpeakerOnce(manual = false)
      }
    }
    viewModelScope.launch {
      while (isActive) {
        delay(1000)
        _state.value?.let { if (it.time > 0) _clock.value = maxOf(_clock.value, it.time) + 1 }
      }
    }
  }

  fun setTheme(t: ThemePref) { _theme.value = t; prefs.edit().putString("theme", t.name).apply() }
  fun toastShown() { _toast.value = null }
  fun say(msg: String) { _toast.value = msg }
  private fun en() = _lang.value == "en"
  private fun t(ru: String, en: String) = if (en()) en else ru

  private fun rebuildTransport() {
    generation++
    api?.transport?.close()
    _viaSpeaker.value = false
    api = when (_link.value) {
      Link.Wifi -> _host.value?.let { Api(HttpTransport(it)) }
      Link.Ble -> _bleDevice.value?.let { Api(BleTransport(getApplication(), it.address, it.name)) }
    }
    _state.value = null
    _online.value = false
    pollJob?.cancel()
    if (api != null) startPolling()
  }

  // ---------- выбор модуля ----------
  fun setHost(h: String) {
    val clean = h.trim().removePrefix("http://").trimEnd('/')
    if (clean.isEmpty()) return
    prefs.edit().putString("host", clean).putString("link", "wifi").apply()
    _host.value = clean
    _link.value = Link.Wifi
    rebuildTransport()
  }

  fun useBle(d: BleDevice) {
    prefs.edit().putString("bleAddr", d.address).putString("bleName", d.name).putString("link", "ble").apply()
    _bleDevice.value = d
    _link.value = Link.Ble
    stopBleScan()
    rebuildTransport()
  }

  fun discover() {
    if (_searching.value) return
    _searching.value = true
    viewModelScope.launch {
      val found = withTimeoutOrNull(8000) { Discovery.find(getApplication()).firstOrNull() }
        ?: probe("wul.local")
      _searching.value = false
      if (found != null) setHost(found)
      else if (api == null) say(t("Модуль не найден. Введите IP-адрес вручную.", "Module not found. Enter its IP address."))
    }
  }

  private suspend fun probe(h: String): String? = runCatching { Api(HttpTransport(h)).state(); h }.getOrNull()

  private var bleJob: Job? = null
  fun scanBle() {
    bleJob?.cancel()
    _bleFound.value = emptyList()
    _bleScanning.value = true
    bleJob = viewModelScope.launch {
      withTimeoutOrNull(12_000) {
        bleScan(getApplication())
          .catch { say(t("Bluetooth выключен или недоступен", "Bluetooth is off or unavailable")) }
          .collect { d ->
            _bleFound.value = (_bleFound.value.filter { it.address != d.address } + d).sortedByDescending { it.rssi }
          }
      }
      _bleScanning.value = false
    }
  }

  fun stopBleScan() { bleJob?.cancel(); _bleScanning.value = false }

  // ---------- опрос ----------
  private fun startPolling() {
    pollJob?.cancel()
    pollJob = viewModelScope.launch {
      while (isActive) {
        poll(force = false)
        delay(1500)
      }
    }
  }

  private suspend fun poll(force: Boolean) {
    val a = api ?: return
    try {
      val s = a.state(if (force) 0 else _state.value?.rev ?: 0)
      if (s != null) {
        _state.value = s
        _clock.value = s.time
        if (s.speaker.name.isNotBlank() && prefs.getString("spkName", null) != s.speaker.name)
          prefs.edit().putString("spkName", s.speaker.name).apply()
        if (s.settings.lang != _lang.value && (s.settings.lang == "ru" || s.settings.lang == "en")) {
          _lang.value = s.settings.lang
          prefs.edit().putString("lang", s.settings.lang).apply()
        }
      }
      _online.value = true
      failures = 0
    } catch (e: Exception) {
      // Отменили сам опрос (смена канала) — выходим. Таймаут транспорта (withTimeout в SPP/BLE — тоже
      // CancellationException) — обычный сбой связи, опрос продолжается.
      currentCoroutineContext().ensureActive()
      if (++failures >= 2) _online.value = false
      // модуль ушёл в Wi-Fi (Звук ×3 / из приложения) — тогда и колонка у телефона пропала; пока она подключена,
      // это лишь сбой канала (затор при музыке) — остаёмся на SPP
      if (_viaSpeaker.value && failures >= 2 && !speakerStillThere()) leaveSpeaker()
    }
  }

  private suspend fun speakerStillThere(): Boolean {
    val ctx = getApplication<Application>()
    if (!hasSppPermissions(ctx)) return false
    return runCatching { connectedSpeaker(ctx, prefs.getString("spkName", null)) }.getOrNull() != null
  }

  // ---------- режим колонки: связь по Bluetooth SPP ----------
  // В режиме колонки у модуля нет Wi-Fi, зато телефон подключён к нему как к колонке (A2DP). Тогда тот же API идёт
  // по Bluetooth SPP. Ищет отдельный цикл (см. init): по Wi-Fi не отвечает или модуль ещё не выбран → есть ли
  // среди подключённых колонок «WakeUpLight-…». В режиме Wi-Fi у модуля Bluetooth выключен — ложных срабатываний нет.
  private var spkBusy = false
  private var spkHoldUntil = 0L

  private suspend fun findSpeakerOnce(manual: Boolean): Boolean {
    if (_viaSpeaker.value) return true
    if (spkBusy || (!manual && System.currentTimeMillis() < spkHoldUntil)) return false
    val ctx = getApplication<Application>()
    if (!hasSppPermissions(ctx)) return false
    spkBusy = true
    val gen = generation
    try {
      val saved = prefs.getString("spkName", null)
      val d = runCatching { connectedSpeaker(ctx, saved) }.getOrNull() ?: return false
      val name = saved ?: runCatching { d.name }.getOrNull() ?: "WakeUpLight"
      val t = SppTransport(ctx, d.address, name)
      val s = runCatching { Api(t).state() }.getOrNull()
      if (s == null || !s.speaker.bt) { t.close(); return false }
      if (_viaSpeaker.value) { t.close(); return true }
      if (gen != generation) { t.close(); return false }   // пока искали, выбрали другой модуль/канал
      useSpeaker(t, s)
      return true
    } finally {
      spkBusy = false
    }
  }

  private fun useSpeaker(t: SppTransport, s: WulState) {
    generation++
    pollJob?.cancel()
    api?.transport?.close()
    api = Api(t)
    _viaSpeaker.value = true
    _state.value = s
    _clock.value = s.time
    _online.value = true
    failures = 0
    startPolling()
  }

  /** Кнопка «Часы в режиме колонки»: найти сразу (разрешение уже спрошено экраном). */
  fun findSpeaker() = viewModelScope.launch {
    if (spkBusy) { say(t("Ищу часы-колонку…", "Looking for the clock speaker…")); return@launch }   // фоновый поиск уже идёт
    if (!findSpeakerOnce(manual = true))
      say(t("Телефон не подключён к часам как к колонке. Выберите «WakeUpLight-…» в настройках Bluetooth телефона.",
        "The phone is not connected to the clock as a speaker. Pick “WakeUpLight-…” in the phone's Bluetooth settings."))
  }

  /** Назад на обычный канал (Wi-Fi): модуль вышел из режима колонки. */
  private fun leaveSpeaker() {
    if (!_viaSpeaker.value) return
    spkHoldUntil = System.currentTimeMillis() + 20_000   // телефон ещё несколько секунд считает колонку подключённой
    rebuildTransport()
    if (api == null && _link.value == Link.Wifi) viewModelScope.launch { delay(8000); discover() }   // адреса не было
  }

  fun refreshNow() = viewModelScope.launch { poll(force = true) }

  fun loadLog() = viewModelScope.launch {
    val a = api ?: return@launch
    runCatching { a.log() }.onSuccess { _log.value = it }
  }

  fun scanWifi() {
    val a = api ?: return
    if (_wifiScanning.value) return
    _wifiScanning.value = true
    viewModelScope.launch {
      runCatching { a.wifiScan() }
        .onSuccess { _wifi.value = it }
        .onFailure { say(t("Не удалось получить список сетей", "Could not scan networks")) }
      _wifiScanning.value = false
    }
  }

  /** Команда модулю; после успеха — сразу свежее состояние. */
  fun act(path: String, body: JSONObject = JSONObject(), ok: String? = null, then: (() -> Unit)? = null) =
    viewModelScope.launch {
      val a = api ?: run { say(t("Модуль не выбран", "No module selected")); return@launch }
      try {
        a.post(path, body)
        ok?.let(::say)
        then?.invoke()
        poll(force = true)
      } catch (e: Exception) {
        say(e.message?.takeIf { e is ApiException } ?: t("Нет связи с часами", "No connection to the clock"))
      }
    }

  fun saveAlarm(a: Alarm, then: () -> Unit) =
    act("/api/alarm", a.copy(enabled = true, label = a.label.trim().ifEmpty { t("Будильник", "Alarm") }).toJson(),
      t("Будильник сохранён", "Alarm saved"), then)
  fun deleteAlarm(id: Int, then: () -> Unit) = act("/api/alarm/delete", JSONObject().put("id", id), t("Будильник удалён", "Alarm deleted"), then)
  fun enableAlarm(id: Int, on: Boolean) = act("/api/alarm/enable", JSONObject().put("id", id).put("on", on))
  fun stopAlarm() = act("/api/alarm/stop")
  fun snooze() = act("/api/alarm/snooze")
  fun dusk(body: JSONObject) = act("/api/dusk", body)
  fun duskStart() = act("/api/dusk/start")
  /** Остановить закат; [off] — ещё и погасить лампу (иначе остаётся на текущей яркости). */
  fun duskStop(off: Boolean = false) = act("/api/dusk/stop", if (off) JSONObject().put("off", true) else JSONObject())
  fun lightOff() = act("/api/light/off")
  /** Табло: надпись на sec секунд (0 — пока не вернут). */
  fun displayText(text: String, sec: Int) = act("/api/display", JSONObject().put("text", text).put("sec", sec))
  fun displayEq(sec: Int = 10) = act("/api/display", JSONObject().put("eq", true).put("fps", 12).put("sec", sec))
  fun displayClock() = act("/api/display", JSONObject().put("clock", true))
  fun settings(body: JSONObject) = act("/api/settings", body)
  fun volume(v: Int) = act("/api/volume", JSONObject().put("v", v))
  fun brightness(v: Int) = act("/api/brightness", JSONObject().put("v", v))
  fun preview(idx: Int, vol: Int) = act("/api/preview", JSONObject().put("idx", idx).put("volume", vol))
  fun previewStop() = act("/api/preview/stop")
  /** Звучание динамика: пресет (off|soft|warm|clear|loud) или ручки — любые из них, модуль сам отметит «свой».
   *  Работает и по Wi-Fi, и в режиме колонки (тот же API по Bluetooth SPP). */
  fun eqPreset(id: String) = act("/api/eq", JSONObject().put("preset", id))
  fun eqSet(hp: Int? = null, body: Int? = null, presence: Int? = null, treble: Int? = null) = act("/api/eq", JSONObject().apply {
    hp?.let { put("hp", it) }; body?.let { put("body", it) }; presence?.let { put("presence", it) }; treble?.let { put("treble", it) }
  })
  fun sound(on: Boolean) = act("/api/sound", JSONObject().put("on", on))
  /** Ручной свет: вкл/выкл и/или уровень 1..20 (во время рассвета/заката модуль ответит 409). */
  fun light(on: Boolean? = null, level: Int? = null) = act("/api/light", JSONObject().apply {
    if (on != null) put("on", on)
    if (level != null) put("level", level.coerceIn(1, 20))
  })
  fun morning(q: Int, e: Int, note: String, then: () -> Unit) =
    act("/api/morning", JSONObject().put("quality", q).put("ease", e).put("note", note), t("Спасибо! Ответ сохранён", "Thanks! Saved")) { then(); loadLog() }

  /** Новая сеть Wi-Fi и/или способ связи; модуль перезагрузится. */
  fun network(ssid: String?, pass: String?, link: String) {
    val b = JSONObject().put("link", link)
    if (ssid != null) b.put("ssid", ssid)
    if (pass != null) b.put("pass", pass)
    act("/api/network", b, t("Сохранено, модуль перезагружается…", "Saved, the module is restarting…")) {
      if (link == "ble" && _link.value == Link.Wifi) {
        // дальше связь только по Bluetooth — переключаемся и ищем модуль
        _link.value = Link.Ble
        prefs.edit().putString("link", "ble").apply()
        rebuildTransport()
      } else if (link == "wifi" && _link.value == Link.Ble) {
        _link.value = Link.Wifi
        prefs.edit().putString("link", "wifi").apply()
        rebuildTransport()
        viewModelScope.launch { delay(8000); discover() }   // модуль поднимает Wi-Fi после перезагрузки
      }
    }
  }

  /** Светомузыка в режиме колонки: эквалайзер на табло и/или лампа в такт. */
  fun speakerLights(showEq: Boolean? = null, lamp: Boolean? = null) = act("/api/speaker", JSONObject().apply {
    if (showEq != null) put("showEq", showEq)
    if (lamp != null) put("lamp", lamp)
  })
  /** Режим колонки: модуль перезагрузится без Wi-Fi — связь с приложением пропадёт до возврата (Звук ×3 на часах). */
  fun speakerMode(on: Boolean) = act("/api/speaker", JSONObject().put("bt", on),
    if (on) t("Модуль переходит в режим колонки. Приложение подключится по Bluetooth, когда телефон подключится к часам.",
      "The module is switching to speaker mode. The app reconnects over Bluetooth once the phone connects to the clock.")
    else t("Возвращаю Wi-Fi — модуль перезагружается…", "Switching back to Wi-Fi — the module is restarting…")) {
    if (!on && _viaSpeaker.value) viewModelScope.launch { delay(2500); leaveSpeaker() }
  }

  fun factoryReset() = act("/api/reset", JSONObject().put("confirm", true),
    t("Сброс выполнен. Модуль поднимет точку WakeUpLight для настройки.", "Reset done. The module will start the WakeUpLight setup hotspot."))

  override fun onCleared() { api?.transport?.close() }
}
