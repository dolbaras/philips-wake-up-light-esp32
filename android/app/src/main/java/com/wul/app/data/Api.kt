package com.wul.app.data

import org.json.JSONArray
import org.json.JSONObject

class ApiException(message: String) : Exception(message)

/** API модуля поверх любого транспорта (Wi-Fi/HTTP или Bluetooth LE) — см. firmware/esp32-wul/API.md. */
class Api(val transport: Transport) {

  /** Состояние; null — не изменилось с ревизии [rev]. */
  suspend fun state(rev: Long = 0): WulState? {
    val (code, text) = transport.request("GET", "/api/state" + if (rev > 0) "?rev=$rev" else "", null)
    if (code == 304) return null
    if (code != 200) throw ApiException("HTTP $code")
    return WulState.from(JSONObject(text))
  }

  suspend fun log(): List<SleepEntry> {
    val (code, text) = transport.request("GET", "/api/log", null)
    if (code != 200) throw ApiException("HTTP $code")
    return JSONArray(text).objects().map(SleepEntry::from)
  }

  suspend fun wifiScan(): List<WifiNet> {
    val (code, text) = transport.request("GET", "/api/wifi/scan", null)
    if (code != 200) throw ApiException("HTTP $code")
    return JSONArray(text).objects()
      .map { WifiNet(it.optString("ssid"), it.optInt("rssi"), it.optBoolean("secure", true)) }
      .filter { it.ssid.isNotBlank() }
      .groupBy { it.ssid }.map { (_, v) -> v.maxBy { it.rssi } }
      .sortedByDescending { it.rssi }
  }

  suspend fun post(path: String, body: JSONObject = JSONObject()) {
    val (code, text) = transport.request("POST", path, body)
    if (code >= 400) throw ApiException(
      runCatching { JSONObject(text).optString("error") }.getOrNull()?.ifBlank { null } ?: "HTTP $code")
  }
}
