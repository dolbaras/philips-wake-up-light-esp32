package com.wul.app.data

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

/** Поиск модуля в локальной сети: ESP32 объявляет себя по mDNS как «wul» (_http._tcp). */
object Discovery {
  fun find(context: Context): Flow<String> = callbackFlow {
    val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    val lock = wifi.createMulticastLock("wul-discovery").apply { setReferenceCounted(false); acquire() }

    val listener = object : NsdManager.DiscoveryListener {
      override fun onDiscoveryStarted(serviceType: String) {}
      override fun onDiscoveryStopped(serviceType: String) {}
      override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { close() }
      override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
      override fun onServiceLost(info: NsdServiceInfo) {}
      override fun onServiceFound(info: NsdServiceInfo) {
        if (!info.serviceName.startsWith("wul", ignoreCase = true)) return
        @Suppress("DEPRECATION")
        nsd.resolveService(info, object : NsdManager.ResolveListener {
          override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) {}
          override fun onServiceResolved(info: NsdServiceInfo) {
            @Suppress("DEPRECATION")
            val addr = info.host?.hostAddress ?: return
            trySend(if (info.port == 80) addr else "$addr:${info.port}")
          }
        })
      }
    }
    nsd.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
    awaitClose {
      runCatching { nsd.stopServiceDiscovery(listener) }
      lock.release()
    }
  }
}
