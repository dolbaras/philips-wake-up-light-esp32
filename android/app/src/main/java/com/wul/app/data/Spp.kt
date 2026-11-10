package com.wul.app.data

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.util.Timer
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.schedule
import kotlin.concurrent.thread
import kotlin.coroutines.resume

/** Разрешения для канала колонки: на Android 12+ — «Устройства поблизости» (BLUETOOTH_CONNECT). */
fun sppPermissions(): Array<String> =
  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) arrayOf(Manifest.permission.BLUETOOTH_CONNECT) else emptyArray()

fun hasSppPermissions(ctx: Context) =
  sppPermissions().all { ctx.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

/**
 * Часы, к которым телефон сейчас подключён как к Bluetooth-колонке (A2DP), или null.
 * [name] — имя модуля, запомненное по Wi-Fi; иначе ищем любое «WakeUpLight-…».
 */
@SuppressLint("MissingPermission")   // вызывается только после hasSppPermissions()
suspend fun connectedSpeaker(ctx: Context, name: String?): BluetoothDevice? {
  val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter ?: return null
  if (!adapter.isEnabled) return null
  return withTimeoutOrNull(3000) {
    suspendCancellableCoroutine { cont ->
      val ok = adapter.getProfileProxy(ctx, object : BluetoothProfile.ServiceListener {
        override fun onServiceConnected(profile: Int, proxy: BluetoothProfile) {
          val d = runCatching {
            proxy.connectedDevices.firstOrNull { dev ->
              val n = dev.name ?: ""
              (name != null && n == name) || n.startsWith("WakeUpLight")
            }
          }.getOrNull()
          runCatching { adapter.closeProfileProxy(profile, proxy) }
          if (cont.isActive) cont.resume(d)
        }
        override fun onServiceDisconnected(profile: Int) {}
      }, BluetoothProfile.A2DP)
      if (!ok && cont.isActive) cont.resume(null)
    }
  }
}

/**
 * Режим колонки: Wi-Fi у модуля выключен, а тот же API идёт по Bluetooth SPP — рядом с музыкой, по уже
 * спаренному телефону. Формат строк — как у Bluetooth LE: {"id","m","p","b"} → {"id","code","body"} + '\n'.
 */
@SuppressLint("MissingPermission")
class SppTransport(private val ctx: Context, val address: String, val name: String) : Transport {
  override val label get() = "Bluetooth · $name"
  private val lock = Mutex()
  private val ids = AtomicInteger(1)
  @Volatile private var socket: BluetoothSocket? = null
  @Volatile private var lines: Channel<String>? = null
  @Volatile private var closed = false            // после close() не переподключаться: запрос, ждавший замок, не откроет сокет-сироту

  private suspend fun ensureConnected() {
    if (socket?.isConnected == true && lines?.isClosedForReceive == false) return
    disconnect()
    check(!closed) { "transport closed" }
    withContext(Dispatchers.IO) {
      val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        ?: throw IllegalStateException("no bluetooth")
      val s = adapter.getRemoteDevice(address).createRfcommSocketToServiceRecord(SPP_UUID)
      socket = s
      val guard = Timer("spp-connect", true)        // connect() сам не прерывается — закрыть сокет через 10 с
      guard.schedule(10_000) { runCatching { s.close() } }
      try { s.connect() } finally { guard.cancel() }   // cancel() гасит и поток таймера
      if (closed) { runCatching { s.close() }; throw IllegalStateException("transport closed") }   // закрыли, пока подключались
      val ch = Channel<String>(8)
      lines = ch
      thread(name = "spp-rx", isDaemon = true) {
        try {
          val r = s.inputStream.bufferedReader(Charsets.UTF_8)
          while (true) { val line = r.readLine() ?: break; ch.trySend(line) }
        } catch (_: Exception) {
        } finally { ch.close() }
      }
    }
    // в режиме колонки у модуля нет интернета — время даёт телефон при каждом подключении
    runCatching { exchange("POST", "/api/time", JSONObject().put("t", phoneLocalTime())) }
  }

  override suspend fun request(method: String, path: String, body: JSONObject?): Pair<Int, String> = lock.withLock {
    try {
      ensureConnected()
      exchange(method, path, body)
    } catch (e: Exception) {
      disconnect()
      throw e
    }
  }

  private suspend fun exchange(method: String, path: String, body: JSONObject?): Pair<Int, String> {
    val s = socket ?: throw IllegalStateException("not connected")
    val ch = lines ?: throw IllegalStateException("not connected")
    val id = ids.getAndIncrement()
    val req = JSONObject().put("id", id).put("m", method).put("p", path)
    if (body != null) req.put("b", body)
    withContext(Dispatchers.IO) {
      s.outputStream.write((req.toString() + "\n").toByteArray(Charsets.UTF_8))
      s.outputStream.flush()
    }
    // опоздавшие ответы на прошлые (оборванные) запросы пропускаем
    repeat(4) {
      val resp = JSONObject(withTimeout(6_000) { ch.receive() })
      if (resp.optInt("id") == id) return resp.optInt("code", 500) to (resp.opt("body")?.toString() ?: "")
    }
    throw IllegalStateException("no reply")
  }

  private fun disconnect() {
    runCatching { socket?.close() }
    socket = null
    lines?.close()
    lines = null
  }

  override fun close() { closed = true; disconnect() }

  companion object {
    val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
  }
}
