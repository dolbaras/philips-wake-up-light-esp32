package com.wul.app.data

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger

/** Bluetooth LE: сервис и характеристики модуля (API.md, раздел «Bluetooth LE»). */
object BleIds {
  val SERVICE: UUID = UUID.fromString("6e7a0001-77b1-4c8e-9d1a-57a4e0c0b1a0")
  val RX: UUID = UUID.fromString("6e7a0002-77b1-4c8e-9d1a-57a4e0c0b1a0")     // телефон → модуль (write)
  val TX: UUID = UUID.fromString("6e7a0003-77b1-4c8e-9d1a-57a4e0c0b1a0")     // модуль → телефон (notify)
  val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
  const val NAME_PREFIX = "WakeUpLight"
}

/** Локальное время телефона «как UTC», секунды — в формате времени модуля. */
fun phoneLocalTime(): Long {
  val ms = System.currentTimeMillis()
  return (ms + java.util.TimeZone.getDefault().getOffset(ms)) / 1000
}

data class BleDevice(val name: String, val address: String, val rssi: Int)

/** Разрешения Bluetooth, которые нужно запросить у пользователя на этой версии Android. */
fun blePermissions(): Array<String> =
  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
    arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
  else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)

fun hasBlePermissions(ctx: Context) =
  blePermissions().all { ctx.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

/** Поиск модулей: имя WakeUpLight-* или наш сервис в рекламе. */
@SuppressLint("MissingPermission")   // вызывается только после hasBlePermissions()
fun bleScan(ctx: Context): Flow<BleDevice> = callbackFlow {
  val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
  val scanner = adapter?.bluetoothLeScanner
  if (scanner == null || !adapter.isEnabled) { close(IllegalStateException("bluetooth off")); return@callbackFlow }
  val cb = object : ScanCallback() {
    override fun onScanResult(callbackType: Int, r: ScanResult) {
      val name = r.scanRecord?.deviceName ?: r.device.name ?: ""
      val ours = name.startsWith(BleIds.NAME_PREFIX, ignoreCase = true) ||
        r.scanRecord?.serviceUuids?.any { it.uuid == BleIds.SERVICE } == true
      if (ours) trySend(BleDevice(name.ifBlank { r.device.address }, r.device.address, r.rssi))
    }
    override fun onScanFailed(errorCode: Int) { close(IllegalStateException("scan failed $errorCode")) }
  }
  scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
  awaitClose { runCatching { scanner.stopScan(cb) } }
}

/**
 * Транспорт по GATT: запрос — JSON-строка + «\n» в RX (кусками по MTU−3), ответ — куски из TX до «\n».
 * Запросы идут строго по одному (Mutex); при ошибке соединение сбрасывается и поднимается заново.
 */
@SuppressLint("MissingPermission")
class BleTransport(private val ctx: Context, val address: String, private val name: String) : Transport {
  override val label get() = name

  private val lock = Mutex()
  private val ids = AtomicInteger(1)
  private var gatt: BluetoothGatt? = null
  private var rx: BluetoothGattCharacteristic? = null
  private var mtu = 23
  private var ready: CompletableDeferred<Unit>? = null
  private var written: CompletableDeferred<Boolean>? = null
  private var reply: CompletableDeferred<String>? = null
  private val inbox = StringBuilder()

  private val callback = object : BluetoothGattCallback() {
    override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
      if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
        if (!g.requestMtu(247)) g.discoverServices()
      } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
        fail(IllegalStateException("disconnected"))
        g.close()
        if (gatt === g) gatt = null
      }
    }

    override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
      if (status == BluetoothGatt.GATT_SUCCESS) this@BleTransport.mtu = mtu
      g.discoverServices()
    }

    override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
      val svc = g.getService(BleIds.SERVICE)
      val r = svc?.getCharacteristic(BleIds.RX)
      val t = svc?.getCharacteristic(BleIds.TX)
      if (status != BluetoothGatt.GATT_SUCCESS || r == null || t == null) { fail(IllegalStateException("no service")); return }
      rx = r
      g.setCharacteristicNotification(t, true)
      val d = t.getDescriptor(BleIds.CCCD) ?: run { ready?.complete(Unit); return }
      val v = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
      if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(d, v)
      else @Suppress("DEPRECATION") run { d.value = v; g.writeDescriptor(d) }
    }

    override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
      if (status == BluetoothGatt.GATT_SUCCESS) ready?.complete(Unit) else fail(IllegalStateException("cccd $status"))
    }

    override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
      written?.complete(status == BluetoothGatt.GATT_SUCCESS)
    }

    // Android 13+: значение приходит параметром; на старых — через characteristic.value
    override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = receive(value)

    @Deprecated("Android < 13")
    @Suppress("DEPRECATION")
    override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
      if (Build.VERSION.SDK_INT < 33) receive(c.value ?: return)
    }
  }

  private fun receive(bytes: ByteArray) {
    synchronized(inbox) {
      inbox.append(String(bytes, Charsets.UTF_8))
      val nl = inbox.indexOf("\n")
      if (nl >= 0) {
        val line = inbox.substring(0, nl)
        inbox.delete(0, nl + 1)
        reply?.complete(line)
      }
    }
  }

  private fun fail(e: Throwable) {
    ready?.completeExceptionally(e)
    written?.complete(false)
    reply?.completeExceptionally(e)
  }

  private suspend fun ensureConnected() {
    if (gatt != null && ready?.isCompleted == true && runCatching { ready?.getCompleted() }.isSuccess) return
    disconnect()
    val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
      ?: throw IllegalStateException("no bluetooth")
    val dev: BluetoothDevice = adapter.getRemoteDevice(address)
    val r = CompletableDeferred<Unit>()
    ready = r
    synchronized(inbox) { inbox.clear() }
    gatt = dev.connectGatt(ctx, false, callback, BluetoothDevice.TRANSPORT_LE)
    withTimeout(12_000) { r.await() }
    // в режиме Bluetooth у модуля нет интернета — время даёт телефон при каждом подключении
    runCatching { exchange("POST", "/api/time", JSONObject().put("t", phoneLocalTime())) }
  }

  private suspend fun writeAll(bytes: ByteArray) {
    val g = gatt ?: throw IllegalStateException("not connected")
    val c = rx ?: throw IllegalStateException("not connected")
    val chunk = (mtu - 3).coerceAtLeast(20)
    var off = 0
    while (off < bytes.size) {
      val part = bytes.copyOfRange(off, minOf(bytes.size, off + chunk))
      val w = CompletableDeferred<Boolean>()
      written = w
      val started = if (Build.VERSION.SDK_INT >= 33)
        g.writeCharacteristic(c, part, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == android.bluetooth.BluetoothStatusCodes.SUCCESS
      else @Suppress("DEPRECATION") run {
        c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        c.value = part
        g.writeCharacteristic(c)
      }
      if (!started || !withTimeout(3000) { w.await() }) throw IllegalStateException("write failed")
      off += part.size
    }
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

  /** Один запрос-ответ по уже открытому соединению (вызывать под [lock]). */
  private suspend fun exchange(method: String, path: String, body: JSONObject?): Pair<Int, String> {
    val id = ids.getAndIncrement()
    val req = JSONObject().put("id", id).put("m", method).put("p", path)
    if (body != null) req.put("b", body)
    val r = CompletableDeferred<String>()
    reply = r
    writeAll((req.toString() + "\n").toByteArray(Charsets.UTF_8))
    return waitFor(id, r, if (path.startsWith("/api/wifi/scan")) 12_000L else 6_000L)
  }

  /** Ждём ответ со своим id; опоздавшие ответы на прошлые (оборванные) запросы пропускаем. */
  private suspend fun waitFor(id: Int, first: CompletableDeferred<String>, timeout: Long): Pair<Int, String> {
    var d = first
    repeat(4) {
      val resp = JSONObject(withTimeout(timeout) { d.await() })
      if (resp.optInt("id") == id) return resp.optInt("code", 500) to (resp.opt("body")?.toString() ?: "")
      d = CompletableDeferred(); reply = d
    }
    throw IllegalStateException("no reply")
  }

  private fun disconnect() {
    gatt?.let { runCatching { it.disconnect(); it.close() } }
    gatt = null
    rx = null
    mtu = 23
  }

  override fun close() = disconnect()
}
