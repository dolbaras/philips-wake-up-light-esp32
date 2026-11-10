package com.wul.app.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/** Канал до модуля: одинаковые запросы «метод + путь + JSON» по Wi-Fi или Bluetooth. Ответ — (код, тело). */
interface Transport {
  val label: String
  suspend fun request(method: String, path: String, body: JSONObject?): Pair<Int, String>
  fun close() {}
}

/** Wi-Fi: обычный HTTP к модулю в домашней сети. */
class HttpTransport(val host: String) : Transport {
  override val label get() = host

  override suspend fun request(method: String, path: String, body: JSONObject?): Pair<Int, String> = withContext(Dispatchers.IO) {
    val c = URL("http://$host$path").openConnection() as HttpURLConnection
    try {
      c.connectTimeout = TIMEOUT
      c.readTimeout = if (path.startsWith("/api/wifi/scan")) 12_000 else TIMEOUT
      if (method == "POST") {
        c.requestMethod = "POST"
        c.doOutput = true
        c.setRequestProperty("Content-Type", "application/json")
        c.outputStream.use { it.write((body ?: JSONObject()).toString().toByteArray()) }
      }
      val code = c.responseCode
      val text = (if (code >= 400) c.errorStream else if (code == 304) null else c.inputStream)
        ?.bufferedReader()?.use { it.readText() } ?: ""
      code to text
    } finally {
      c.disconnect()
    }
  }

  private companion object { const val TIMEOUT = 4000 }
}
