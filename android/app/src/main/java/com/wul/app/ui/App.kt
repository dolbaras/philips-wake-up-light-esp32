package com.wul.app.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Alarm
import androidx.compose.material.icons.rounded.Bedtime
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Insights
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import kotlinx.coroutines.launch
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.wul.app.Link
import com.wul.app.MainViewModel
import com.wul.app.data.Alarm

private enum class Tab(val ru: String, val en: String, val icon: ImageVector) {
  Wake("Будильник", "Wake up", Icons.Rounded.Alarm),
  Sleep("Сон", "Sleep", Icons.Rounded.Bedtime),
  Stats("Статистика", "Stats", Icons.Rounded.Insights),
  Settings("Настройки", "Settings", Icons.Rounded.Tune),
  Info("Инфо", "Info", Icons.Rounded.Info),
}

@Composable
fun App(vm: MainViewModel) {
  val c = LocalWul.current
  val state by vm.state.collectAsStateWithLifecycle()
  val host by vm.host.collectAsStateWithLifecycle()
  val link by vm.link.collectAsStateWithLifecycle()
  val ble by vm.bleDevice.collectAsStateWithLifecycle()
  val online by vm.online.collectAsStateWithLifecycle()
  val searching by vm.searching.collectAsStateWithLifecycle()
  val bleFound by vm.bleFound.collectAsStateWithLifecycle()
  val bleScanning by vm.bleScanning.collectAsStateWithLifecycle()
  val log by vm.log.collectAsStateWithLifecycle()
  val toast by vm.toast.collectAsStateWithLifecycle()
  val theme by vm.theme.collectAsStateWithLifecycle()
  val now by vm.clock.collectAsStateWithLifecycle()
  val viaSpeaker by vm.viaSpeaker.collectAsStateWithLifecycle()

  var tab by rememberSaveable { mutableStateOf(Tab.Wake) }
  var editing by remember { mutableStateOf<Alarm?>(null) }
  var editorOpen by remember { mutableStateOf(false) }
  var session by remember { mutableIntStateOf(0) }
  var changing by remember { mutableStateOf(false) }
  var previewing by remember { mutableIntStateOf(-1) }
  val snack = remember { SnackbarHostState() }
  val target = if (link == Link.Ble) ble?.name else host
  val needConnect = (target == null && !viaSpeaker) || changing   // часы-колонку нашли и без адреса Wi-Fi
  // нашли модуль автоматически (поиск сменил адрес) — закрыть экран подключения
  LaunchedEffect(target) { changing = false }
  LaunchedEffect(viaSpeaker) { if (viaSpeaker) changing = false }   // нашли часы-колонку — экран подключения закрыть

  // показ — в своей корутине: сброс toast перезапускает LaunchedEffect и иначе отменил бы снекбар
  val scope = rememberCoroutineScope()
  LaunchedEffect(toast) {
    toast?.let { msg -> vm.toastShown(); snack.currentSnackbarData?.dismiss(); scope.launch { snack.showSnackbar(msg) } }
  }

  Box(Modifier.fillMaxSize().background(c.bg)) {
    Scaffold(
      containerColor = c.bg,
      snackbarHost = {
        SnackbarHost(snack) { Snackbar(it, containerColor = c.surface2, contentColor = c.text, shape = CtlShape) }
      },
      bottomBar = {
        AnimatedVisibility(state != null && !needConnect && state?.setup != true && state?.speaker?.bt != true, enter = fadeIn(), exit = fadeOut()) {
          NavigationBar(containerColor = c.surface, tonalElevation = 0.dp) {
            Tab.entries.forEach { t ->
              val sel = tab == t
              val k by animateFloatAsState(if (sel) 1.15f else 1f, spring(Spring.DampingRatioMediumBouncy), label = "nav")
              NavigationBarItem(
                selected = sel, onClick = { tab = t },
                icon = {
                  Box(Modifier.graphicsLayer { scaleX = k; scaleY = k }) {
                    if (t == Tab.Stats && state?.question == true) BadgedBox(badge = { Badge(containerColor = c.dawn) }) { Icon(t.icon, null) }
                    else Icon(t.icon, null)
                  }
                },
                label = { Text(tr(t.ru, t.en), fontSize = 11.sp, maxLines = 1) },
                colors = NavigationBarItemDefaults.colors(
                  selectedIconColor = c.dawn, selectedTextColor = c.dawn, indicatorColor = c.dawn.copy(alpha = .14f),
                  unselectedIconColor = c.muted, unselectedTextColor = c.muted),
              )
            }
          }
        }
      },
    ) { pad ->
      Box(Modifier.fillMaxSize().padding(pad)) {
        val s = state
        when {
          needConnect -> ConnectScreen(vm, link, searching, host, bleFound, bleScanning,
            if (changing) ({ changing = false }) else null, onDone = { changing = false })
          s == null -> Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally) {
            Spacer(Modifier.weight(1f))
            CircularProgressIndicator(color = c.dawn)
            Muted(tr("Подключаюсь к ", "Connecting to ") + (if (viaSpeaker) "Bluetooth" else target ?: "—") + "…", Modifier.padding(top = 16.dp), 15)
            if (!online) TextButton(onClick = { changing = true }, modifier = Modifier.padding(top = 12.dp)) {
              Text(tr("Сменить модуль", "Change module"), color = c.dawn, fontSize = 15.sp)
            }
            if (!online) SpeakerFindButton(vm, Modifier.padding(horizontal = 32.dp, vertical = 8.dp))
            Spacer(Modifier.weight(1f))
          }
          s.speaker.bt -> SpeakerScreen(s, vm)            // модуль — колонка, связь по Bluetooth SPP (раньше «не настроен»)
          s.setup -> SetupScreen(s) { changing = true }
          else -> Column(Modifier.fillMaxSize()) {
            StatusLine(online, s.device.connected, link, s.device.docked)
            // связь пропала (часто — колонку включили кнопками на часах): найти часы-колонку, спросив разрешение
            AnimatedVisibility(!online) { SpeakerFindButton(vm, Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 4.dp)) }
            AnimatedContent(tab, transitionSpec = {
              val dir = if (targetState.ordinal > initialState.ordinal) 1 else -1
              (slideInHorizontally(tween(280)) { dir * it / 6 } + fadeIn(tween(280))) togetherWith
                (slideOutHorizontally(tween(220)) { -dir * it / 6 } + fadeOut(tween(180)))
            }, modifier = Modifier.weight(1f), label = "tabs") { t ->
              when (t) {
                Tab.Wake -> WakeTab(s, now, vm) { editing = it; session++; editorOpen = true }
                Tab.Sleep -> SleepTab(s, vm, previewing) { previewing = it }
                Tab.Stats -> StatsTab(s, vm, log)
                Tab.Settings -> SettingsTab(s, vm, theme, now) { changing = true }
                Tab.Info -> InfoTab(s)
              }
            }
          }
        }
      }
    }

    // поверх всего — закрывает и нижнюю панель
    state?.let { if (!needConnect) FullScreenState(it, now, vm) }
    AlarmSheet(editorOpen, editing, session, vm) { editorOpen = false }
  }
}

private fun linkIcon(link: Link) = if (link == Link.Ble) Icons.Rounded.Bluetooth else Icons.Rounded.Wifi

@Composable
private fun StatusLine(online: Boolean, clock: Boolean, link: Link, docked: Boolean = false) {
  val c = LocalWul.current
  Row(
    Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 6.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    Dot(online && clock)
    Spacer(Modifier.width(6.dp))
    Muted(
      when {
        !online -> tr("нет связи с модулем", "module offline")
        docked -> tr("часами управляет iPhone/iPad в доке", "the docked iPhone/iPad controls the clock")
        !clock -> tr("модуль на связи, часы — нет", "module online, clock offline")
        else -> tr("часы на связи", "clock connected")
      }, size = 13,
    )
    Spacer(Modifier.weight(1f))
    Icon(linkIcon(link), null, tint = c.muted, modifier = Modifier.size(16.dp))
  }
}
