package com.wul.app.ui

import androidx.activity.compose.BackHandler
import androidx.compose.ui.platform.LocalContext
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.VolumeDown
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.BrightnessHigh
import androidx.compose.material.icons.rounded.BrightnessLow
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Lightbulb
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Pin
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material.icons.rounded.Remove
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.Science
import androidx.compose.material.icons.rounded.Snooze
import androidx.compose.material.icons.rounded.SettingsRemote
import androidx.compose.material.icons.rounded.Speaker
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material.icons.rounded.WifiFind
import androidx.compose.material.icons.rounded.WbSunny
import androidx.compose.material.icons.rounded.NightsStay
import androidx.compose.material.icons.rounded.Quiz
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import com.wul.app.data.Mode
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wul.app.MainViewModel
import com.wul.app.ThemePref
import com.wul.app.data.ButtonEvent
import com.wul.app.data.REGIONS
import com.wul.app.data.WifiNet
import com.wul.app.data.WulState
import com.wul.app.data.hasSppPermissions
import com.wul.app.data.sppPermissions
import org.json.JSONObject

private enum class Page { Main, Buttons, Network, Language, Display, Speaker }

@Composable
fun SettingsTab(s: WulState, vm: MainViewModel, theme: ThemePref, now: Long, onChangeModule: () -> Unit) {
  var page by rememberSaveable { mutableStateOf(Page.Main) }
  BackHandler(enabled = page != Page.Main) { page = Page.Main }
  AnimatedContent(page, transitionSpec = {
    val fwd = targetState != Page.Main
    (slideInHorizontally(tween(300)) { if (fwd) it / 3 else -it / 3 } + fadeIn(tween(300))) togetherWith
      (slideOutHorizontally(tween(250)) { if (fwd) -it / 3 else it / 3 } + fadeOut(tween(200)))
  }, label = "settings") { p ->
    when (p) {
      Page.Main -> SettingsMain(s, vm, theme, onChangeModule) { page = it }
      Page.Buttons -> ButtonsPage(s, vm) { page = Page.Main }
      Page.Network -> NetworkPage(s, vm) { page = Page.Main }
      Page.Language -> LanguagePage(s, vm) { page = Page.Main }
      Page.Display -> DisplayPage(s, vm, now) { page = Page.Main }
      Page.Speaker -> SpeakerPage(s, vm) { page = Page.Main }
    }
  }
}

@Composable
fun SubTitle(text: String, onBack: () -> Unit) {
  Row(Modifier.fillMaxWidth().padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically) {
    IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, tr("Назад", "Back"), tint = LocalWul.current.text) }
    Text(text, fontSize = 26.sp, fontWeight = FontWeight.Bold, color = LocalWul.current.text)
  }
}

// ---------------------------------------------------------------- главная страница настроек
@Composable
private fun SettingsMain(s: WulState, vm: MainViewModel, theme: ThemePref, onChangeModule: () -> Unit, go: (Page) -> Unit) {
  val c = LocalWul.current
  var confirmReset by remember { mutableStateOf(false) }
  TabList {
    item { Title(tr("Настройки", "Settings")) }
    item { SectionTitle(tr("Часы", "Clock")) }
    item {
      WCard(Modifier.reveal(0)) {
        SliderRow(tr("Яркость табло", "Display brightness"), s.settings.displayBrightness, 0..7,
          startIcon = Icons.Rounded.BrightnessLow, endIcon = Icons.Rounded.BrightnessHigh) { vm.brightness(it) }
        SliderRow(tr("Громкость", "Volume"), s.volume, 0..20,
          startIcon = Icons.AutoMirrored.Rounded.VolumeDown, endIcon = Icons.AutoMirrored.Rounded.VolumeUp) { vm.volume(it) }
      }
    }
    item { SectionTitle(tr("Модуль", "Module")) }
    item {
      WCard(Modifier.reveal(1), pad = 10) {
        NavRow(Icons.Rounded.SettingsRemote, c.dawn, tr("Кнопки часов", "Clock buttons"),
          (if (s.clock.lightOn) tr("свет вкл · ", "light on · ") + s.clock.lightLevel else tr("свет выкл", "light off")) +
            (if (s.clock.soundOn) tr(" · звук вкл", " · sound on") else "")) { go(Page.Buttons) }
        NavRow(if (s.net.link == "ble") Icons.Rounded.Bluetooth else Icons.Rounded.Wifi, c.dusk, tr("Сеть и связь", "Network & connection"),
          if (s.net.link == "ble") "Bluetooth · ${s.net.ble}" else "Wi-Fi · ${s.net.ssid.ifBlank { "—" }}") { go(Page.Network) }
        NavRow(Icons.Rounded.Pin, c.dawn, tr("Табло", "Display"),
          when (s.display.mode) {
            "text" -> tr("надпись «", "text “") + s.display.text + tr("»", "”")
            "eq" -> tr("эквалайзер", "equalizer")
            else -> tr("часы · своя надпись, эквалайзер", "clock · custom text, equalizer")
          }) { go(Page.Display) }
        if (s.speaker.available) NavRow(Icons.Rounded.Speaker, c.dusk, tr("Колонка Bluetooth", "Bluetooth speaker"),
          (if (s.eq.available) tr("звук: ", "sound: ") + (eqPresetNames().firstOrNull { it.first == s.eq.preset }?.second ?: s.eq.preset) + " · " else "") +
          tr("светомузыка: ", "light show: ") + lightShowName(s)) { go(Page.Speaker) }
        NavRow(Icons.Rounded.Language, c.ok, tr("Язык и регион", "Language & region"),
          (if (s.settings.lang == "en") "English" else "Русский") + " · " + regionName(s.settings.region)) { go(Page.Language) }
      }
    }
    item { SectionTitle(tr("Сон", "Sleep")) }
    item {
      WCard(Modifier.reveal(2), pad = 12) {
        SwitchRow(tr("Учитывать время отбоя", "Track bedtime"), s.settings.trackBedtime,
          tr("начало заката — в статистику", "sunset start goes to stats"), c.dusk, Icons.Rounded.NightsStay) {
          vm.settings(JSONObject().put("trackBedtime", it))
        }
        SwitchRow(tr("Утренний опрос", "Morning survey"), s.settings.morningQuestion,
          tr("30 минут после подъёма", "30 minutes after waking"), c.dawn, Icons.Rounded.Quiz) {
          vm.settings(JSONObject().put("morningQuestion", it))
        }
        Row(Modifier.padding(top = 10.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
          IconBadge(Icons.Rounded.Lightbulb, c.dawn, 36); Spacer(Modifier.width(12.dp))
          Column {
            Text(tr("Свет после будильника", "Light after the alarm"), fontSize = 16.sp, color = c.text)
            Muted(tr("погасить через… Если сами тронете свет — не погасит.", "turn off after… If you touch the light yourself, it stays on."), size = 13)
          }
        }
        val offs = listOf(0, 5, 15, 30, 60)
        Pills(listOf(tr("Оставить", "Keep"), "5", "15", "30", "60"), { offs[it] == s.settings.lightOffAfterAlarm }) {
          vm.settings(JSONObject().put("lightOffAfterAlarm", offs[it]))
        }
        Spacer(Modifier.height(4.dp))
      }
    }
    item { SectionTitle(tr("Оформление", "Appearance")) }
    item {
      WCard(Modifier.reveal(3)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(bottom = 10.dp)) {
          IconBadge(Icons.Rounded.Palette, c.dawn, 36); Spacer(Modifier.width(12.dp))
          Text(tr("Тема", "Theme"), fontSize = 16.sp, color = c.text)
        }
        val opts = listOf(ThemePref.Dark to tr("Тёмная", "Dark"), ThemePref.Light to tr("Светлая", "Light"), ThemePref.Auto to tr("Авто", "Auto"))
        Pills(opts.map { it.second }, { opts[it].first == theme }) { vm.setTheme(opts[it].first) }
      }
    }
    item { SectionTitle(tr("Устройство", "Device")) }
    item {
      WCard(Modifier.reveal(4)) {
        KeyValue(tr("Часы", "Clock"), s.device.name.ifBlank { "—" })
        KeyValue(tr("Прошивка часов", "Clock firmware"), s.device.fw.ifBlank { "—" })
        KeyValue(tr("Связь с часами", "Clock link"), when {
          s.device.dock == "device" -> tr("в доке iPhone/iPad", "iPhone/iPad docked")
          s.device.docked -> tr("отпущена вручную", "released manually")
          s.device.connected -> tr("на связи", "connected")
          else -> tr("нет", "no")
        })
        KeyValue(tr("Время модуля", "Module time"), if (s.time > 0) hm(s.time) else tr("ждём время", "waiting for time"))
        KeyValue(tr("Модуль", "Module"), s.device.esp.ifBlank { "—" })
        Spacer(Modifier.height(8.dp))
        GhostButton(tr("Сменить модуль", "Change module"), c.text, icon = Icons.Rounded.SwapHoriz, onClick = onChangeModule)
      }
    }
    item {
      WCard(Modifier.reveal(5), pad = 10) {
        NavRow(Icons.Rounded.RestartAlt, c.danger, tr("Сброс к заводским", "Factory reset"),
          tr("стереть будильники, сеть и настройки", "erase alarms, network and settings")) { confirmReset = true }
      }
    }
  }
  if (confirmReset) AlertDialog(
    onDismissRequest = { confirmReset = false },
    icon = { Icon(Icons.Rounded.RestartAlt, null, tint = c.danger) },
    title = { Text(tr("Сбросить модуль?", "Reset the module?")) },
    text = { Text(tr("Будильники, журнал сна, Wi-Fi и настройки будут стёрты. Модуль перезагрузится и поднимет точку WakeUpLight для настройки.",
      "Alarms, sleep log, Wi-Fi and settings will be erased. The module restarts into the WakeUpLight setup hotspot.")) },
    confirmButton = { TextButton(onClick = { confirmReset = false; vm.factoryReset() }) { Text(tr("Сбросить", "Reset"), color = c.danger) } },
    dismissButton = { TextButton(onClick = { confirmReset = false }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
    containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
  )
}

// ---------------------------------------------------------------- колонка Bluetooth
@Composable
private fun lightShowName(s: WulState) = when {
  s.speaker.showEq && s.speaker.lamp -> tr("табло и лампа", "display and lamp")
  s.speaker.showEq -> tr("табло", "display")
  s.speaker.lamp -> tr("лампа", "lamp")
  else -> tr("выкл", "off")
}

@Composable
private fun SpeakerPage(s: WulState, vm: MainViewModel, onBack: () -> Unit) {
  val c = LocalWul.current
  val ctx = LocalContext.current
  var confirm by remember { mutableStateOf(false) }
  // без «Устройств поблизости» приложение не увидит часы-колонку по Bluetooth — спросить до включения
  val need = tr("Без разрешения «Устройства поблизости» приложение не найдёт часы-колонку — режим не включаю",
    "Without the “Nearby devices” permission the app can't find the clock speaker — speaker mode not turned on")
  val perms = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { r ->
    if (r.values.all { it }) vm.speakerMode(true) else vm.say(need)
  }
  val name = s.speaker.name.ifBlank { "WakeUpLight" }
  TabList {
    item { SubTitle(tr("Колонка Bluetooth", "Bluetooth speaker"), onBack) }
    item {
      WCard(Modifier.reveal(0), pad = 12) {
        SwitchRow(tr("Режим колонки", "Speaker mode"), s.speaker.bt,
          tr("телефон играет музыку через часы", "your phone plays music through the clock"), c.dusk, Icons.Rounded.Speaker) {
          if (it) confirm = true else vm.speakerMode(false)
        }
        Spacer(Modifier.height(4.dp))
        Muted(tr("Модуль перезагрузится без Wi-Fi (колонке и Wi-Fi вместе не хватает памяти). Когда телефон подключится к часам " +
          "по Bluetooth, приложение откроет экран колонки — оттуда же и вернуть Wi-Fi. Будильники работают.",
          "The module restarts without Wi-Fi (the speaker and Wi-Fi together don't fit in memory). Once the phone connects to the clock " +
          "over Bluetooth, the app opens the speaker screen — Wi-Fi is switched back from there. Alarms keep working."), size = 13)
        Spacer(Modifier.height(8.dp))
      }
    }
    item { SectionTitle(tr("Светомузыка", "Light show")) }
    item {
      WCard(Modifier.reveal(1), pad = 12) {
        SwitchRow(tr("Эквалайзер на табло", "Equalizer on the display"), s.speaker.showEq,
          tr("цифры прыгают в такт, пока играет музыка", "digits bounce to the music while it plays"), c.dawn, Icons.Rounded.GraphicEq) {
          vm.speakerLights(showEq = it)
        }
        SwitchRow(tr("Лампа в такт", "Lamp to the beat"), s.speaker.lamp,
          tr("яркость следует за громкостью; после музыки — как было", "brightness follows loudness; restored after the music"),
          c.dawn, Icons.Rounded.Lightbulb) {
          vm.speakerLights(lamp = it)
        }
      }
    }
    if (s.eq.available) {
      item { SectionTitle(tr("Звучание динамика", "Speaker sound")) }
      item { EqCard(s, vm, listen = !s.speaker.bt, Modifier.reveal(2)) }
    }
    item { SectionTitle(tr("Как подключить", "How to connect")) }
    item {
      WCard(Modifier.reveal(2)) {
        Legend(Icons.Rounded.Speaker, tr("Включить", "Turn on"),
          tr("переключателем выше или «Звук» 3 раза подряд на часах", "with the switch above or press Sound 3 times on the clock"))
        Legend(Icons.Rounded.Bluetooth, tr("Подключить телефон", "Pair the phone"),
          tr("в настройках Bluetooth телефона выберите «$name»", "in the phone's Bluetooth settings pick “$name”"))
        Legend(Icons.AutoMirrored.Rounded.VolumeUp, tr("Громкость", "Volume"),
          tr("кнопками телефона или +/− на часах — они связаны", "with the phone buttons or +/− on the clock — they are linked"))
        Legend(Icons.Rounded.Wifi, tr("Вернуть Wi-Fi", "Back to Wi-Fi"),
          tr("кнопкой на экране колонки в приложении или «Звук» 3 раза подряд на часах; без телефона — сам через 10 минут",
            "with the button on the app's speaker screen or Sound 3 times on the clock; without a phone — by itself after 10 minutes"))
      }
    }
  }
  if (confirm) AlertDialog(
    onDismissRequest = { confirm = false },
    icon = { Icon(Icons.Rounded.Speaker, null, tint = c.dusk) },
    title = { Text(tr("Включить режим колонки?", "Turn on speaker mode?")) },
    text = { Text(tr("Модуль перезагрузится без Wi-Fi и сам подключится к телефону, с которым уже играл. В первый раз выберите «$name» " +
      "в настройках Bluetooth телефона. Приложение переключится на экран колонки.",
      "The module restarts without Wi-Fi and reconnects to the phone it played with before. The first time, pick “$name” " +
      "in the phone's Bluetooth settings. The app switches to the speaker screen.")) },
    confirmButton = { TextButton(onClick = {
      confirm = false
      if (hasSppPermissions(ctx)) vm.speakerMode(true) else perms.launch(sppPermissions())
    }) { Text(tr("Включить", "Turn on"), color = c.dusk) } },
    dismissButton = { TextButton(onClick = { confirm = false }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
    containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
  )
}

// ---------------------------------------------------------------- кнопки часов
private fun keyOf(id: String) = when (id) { "up", "volUp" -> "plus"; "down", "volDown" -> "minus"; "test" -> "light"; else -> id }

@Composable
private fun ButtonsPage(s: WulState, vm: MainViewModel, onBack: () -> Unit) {
  val c = LocalWul.current
  val last = s.buttons.lastOrNull()
  TabList {
    item { SubTitle(tr("Кнопки часов", "Clock buttons"), onBack) }
    item {
      WCard(Modifier.reveal(0)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Text(tr("Пульт часов", "Clock remote"), color = c.text, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.weight(1f))
          AnimatedVisibility(s.clock.test, enter = fadeIn(), exit = fadeOut()) {
            Row(Modifier.clip(RoundedCornerShape(50)).background(c.danger.copy(alpha = .16f)).padding(horizontal = 10.dp, vertical = 4.dp),
              verticalAlignment = Alignment.CenterVertically) {
              Icon(Icons.Rounded.Science, null, tint = c.danger, modifier = Modifier.size(14.dp)); Spacer(Modifier.width(4.dp))
              Text("test", color = c.danger, fontSize = 12.sp, fontWeight = FontWeight.Bold)
            }
          }
        }
        Muted(tr("Нажимайте здесь — как на самих часах. Нажатия на часах тоже подсвечиваются.",
          "Tap here just like on the clock. Presses on the clock light up too."), size = 13)
        Spacer(Modifier.height(14.dp))
        val snoozeOnlyRinging = tr("Снуз работает во время звонка", "Snooze works while the alarm rings")
        ClockPanel(last, s.clock.pad) { key ->
          when (key) {
            "light" -> vm.light(on = !s.clock.lightOn)
            "plus", "minus" -> {
              val d = if (key == "plus") 1 else -1
              if (s.clock.pad == "light") vm.light(level = (s.clock.lightLevel + d).coerceIn(1, 20))
              else vm.volume((s.volume + d).coerceIn(0, 20))
            }
            "sound" -> vm.sound(!s.clock.soundOn)
            "snooze" -> if (s.mode == Mode.Ringing) vm.snooze() else vm.say(snoozeOnlyRinging)
          }
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
          Muted(tr("+/− сейчас: ", "+/− now: "), size = 13)
          AnimatedContent(s.clock.pad == "light", transitionSpec = { fadeIn(tween(200)) togetherWith fadeOut(tween(150)) }, label = "pad") { l ->
            Text(if (l) tr("яркость лампы", "lamp brightness") + " · ${s.clock.lightLevel}" else tr("громкость", "volume") + " · ${s.volume}",
              fontSize = 13.sp, color = c.dawn, fontWeight = FontWeight.SemiBold)
          }
        }
      }
    }
    item {
      WCard(Modifier.reveal(1), pad = 12) {
        SwitchRow(tr("Лампа", "Lamp"), s.clock.lightOn,
          (if (s.clock.lightOn) tr("горит, яркость ", "on, brightness ") + "${s.clock.lightLevel} / 20 · "
          else tr("выключена · ", "off · ")) + tr("гаснет за 3–5 с", "fades out in 3–5 s"),
          c.dawn, Icons.Rounded.Lightbulb) { vm.light(on = it) }
        SliderRow(tr("Яркость лампы", "Lamp brightness"), s.clock.lightLevel, 1..20,
          brush = Brush.horizontalGradient(listOf(dawnColor(.15f), dawnColor(.55f), dawnColor(.9f))),
          startIcon = Icons.Rounded.BrightnessLow, endIcon = Icons.Rounded.BrightnessHigh) { vm.light(on = true, level = it) }
        LevelBar(if (s.clock.lightOn) s.clock.lightLevel else 0)
        Spacer(Modifier.height(6.dp))
        SwitchRow(tr("Звук кнопкой Звук", "Sound button"), s.clock.soundOn,
          tr("мелодия: ", "melody: ") + soundName(s.settings.soundIdx), c.dawn, Icons.Rounded.MusicNote) { vm.sound(it) }
      }
    }
    item { SectionTitle(tr("Мелодия кнопки Звук", "Sound button melody")) }
    item {
      SoundList(s.settings.soundIdx, -1, c.dawn, onPick = { vm.settings(JSONObject().put("soundIdx", it)) }) { i ->
        vm.preview(i, s.volume.coerceAtLeast(6))
      }
    }
    item { SectionTitle(tr("Что делают кнопки", "What the buttons do")) }
    item {
      WCard(Modifier.reveal(2)) {
        Legend(Icons.Rounded.WbSunny, tr("Свет", "Light"), tr("включить или выключить лампу", "turn the lamp on or off"))
        Legend(Icons.Rounded.Add, "+ / −", tr("при включённом свете — яркость лампы 1–20; после кнопки Звук — громкость",
          "with the light on — lamp brightness 1–20; after the Sound button — volume"))
        Legend(Icons.Rounded.MusicNote, tr("Звук", "Sound"), tr("включить или выключить мелодию (играет модуль)", "play or stop the melody (played by the module)"))
        Legend(Icons.Rounded.Snooze, tr("Снуз", "Snooze"), tr("во время будильника — повтор через заданные минуты", "while ringing — snooze for the set minutes"))
        Legend(Icons.Rounded.Science, tr("Свет 5 с", "Light 5 s"), tr("встроенный режим test часов; выход — так же", "the clock’s built-in test mode; same to exit"))
        Legend(Icons.Rounded.RestartAlt, tr("Снуз ×10 за 6 с", "Snooze ×10 in 6 s"), tr("сброс к заводским (табло мигнёт 3 раза)", "factory reset (display blinks 3 times)"))
      }
    }
    item {
      WCard(Modifier.reveal(3), pad = 12) {
        SwitchRow(tr("Сброс сочетанием кнопок", "Reset by button combo"), s.settings.resetCombo,
          tr("Снуз 10 раз за 6 секунд вне будильника", "Snooze 10 times in 6 seconds outside an alarm"), c.danger, Icons.Rounded.RestartAlt) {
          vm.settings(JSONObject().put("resetCombo", it))
        }
        SwitchRow(tr("Индикатор на плате", "Board indicator"), s.settings.boardLed,
          tr("синий светодиод модуля мигает, показывая связь; выключен — не светит совсем", "the module’s blue LED blinks to show the link; off — stays dark"),
          c.dusk, Icons.Rounded.Memory) {
          vm.settings(JSONObject().put("boardLed", it))
        }
      }
    }
    if (s.buttons.isNotEmpty()) {
      item { SectionTitle(tr("Последние нажатия", "Recent presses")) }
      val newest = s.buttons.last().ms
      items(s.buttons.takeLast(8).reversed(), key = { it.ms.toString() + it.id }) { b ->
        PressRow(b, newest, Modifier.animateItem())
      }
    }
  }
}

@Composable
private fun Legend(icon: ImageVector, title: String, text: String) {
  val c = LocalWul.current
  Row(Modifier.padding(vertical = 6.dp)) {
    IconBadge(icon, c.dawn, 32)
    Spacer(Modifier.width(12.dp))
    Column {
      Text(title, color = c.text, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
      Muted(text, size = 13)
    }
  }
}

@Composable
private fun LevelBar(level: Int) {
  val c = LocalWul.current
  Row(Modifier.fillMaxWidth().padding(start = 48.dp, top = 2.dp), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
    repeat(20) { i ->
      val on = i < level
      val col by animateColorAsState(if (on) dawnColor(.3f + .7f * i / 19f) else c.surface2, tween(250, delayMillis = i * 15), label = "lv")
      Box(Modifier.weight(1f).height(10.dp).clip(RoundedCornerShape(3.dp)).background(col))
    }
  }
}

@Composable
private fun buttonLabel(b: ButtonEvent): String = when (b.id) {
  "light" -> if (b.level > 0) tr("Свет включён", "Light on") else tr("Свет выключен", "Light off")
  "up" -> tr("Яркость + → ", "Brightness + → ") + b.level
  "down" -> tr("Яркость − → ", "Brightness − → ") + b.level
  "sound" -> tr("Звук", "Sound")
  "volUp" -> tr("Громкость +", "Volume +")
  "volDown" -> tr("Громкость −", "Volume −")
  "snooze" -> tr("Снуз", "Snooze")
  "test" -> tr("Режим test", "Test mode")
  else -> b.id
}

@Composable
private fun PressRow(b: ButtonEvent, newest: Long, modifier: Modifier) {
  val c = LocalWul.current
  val icon = when (keyOf(b.id)) {
    "light" -> Icons.Rounded.WbSunny; "plus" -> Icons.Rounded.Add; "minus" -> Icons.Rounded.Remove
    "sound" -> Icons.Rounded.MusicNote; "snooze" -> Icons.Rounded.Snooze; else -> Icons.Rounded.Tune
  }
  Row(modifier.fillMaxWidth().clip(CtlShape).background(c.surface).padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
    IconBadge(icon, c.dawn, 32)
    Spacer(Modifier.width(12.dp))
    Text(buttonLabel(b), color = c.text, modifier = Modifier.weight(1f))
    val ago = ((newest - b.ms) / 1000).toInt()
    Muted(if (ago <= 0) tr("последнее", "latest") else tr("на $ago с раньше", "$ago s earlier"), size = 12)
  }
}

/**
 * Пульт — схема кнопок часов (Снуз сверху, ниже Свет, −, +, Звук), нажимается как настоящие.
 * Вспыхивает и нажатая здесь кнопка, и нажатая на самих часах; точка — к чему относятся +/−.
 */
@Composable
private fun ClockPanel(last: ButtonEvent?, pad: String, onPress: (String) -> Unit) {
  val c = LocalWul.current
  val haptic = LocalHapticFeedback.current
  val flash = remember { Animatable(0f) }
  var hot by remember { mutableStateOf<String?>(null) }
  var hits by remember { mutableIntStateOf(0) }
  LaunchedEffect(last?.ms, last?.id) { if (last != null) { hot = keyOf(last.id); hits++ } }
  LaunchedEffect(hits) {
    if (hits > 0) { flash.snapTo(1f); kotlinx.coroutines.delay(700); flash.animateTo(0f, tween(1200)) }
  }
  @Composable
  fun Key(id: String, icon: ImageVector, label: String, modifier: Modifier, marked: Boolean = false) {
    val k = if (hot == id) flash.value else 0f
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
      Box(
        Modifier.fillMaxWidth().height(58.dp)
          .clip(RoundedCornerShape(16.dp))
          .bouncy(onClick = {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
            hot = id; hits++
            onPress(id)
          })
          .background(androidx.compose.ui.graphics.lerp(c.surface2, c.dawn, k))
          .border(1.dp, if (marked) c.dawn.copy(alpha = .6f) else c.border, RoundedCornerShape(16.dp)),
        contentAlignment = Alignment.Center,
      ) {
        Icon(icon, label, tint = androidx.compose.ui.graphics.lerp(if (marked) c.dawn else c.text, c.onAccent, k))
        if (marked) Box(Modifier.align(Alignment.TopEnd).padding(7.dp).size(7.dp).clip(CircleShape).background(c.dawn))
      }
      Spacer(Modifier.height(4.dp))
      Text(label, fontSize = 12.sp, color = if (k > .3f || marked) c.dawn else c.muted)
    }
  }
  Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(26.dp)).background(c.bg).padding(14.dp)) {
    Key("snooze", Icons.Rounded.Snooze, tr("Снуз", "Snooze"), Modifier.fillMaxWidth())
    Spacer(Modifier.height(12.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
      Key("light", Icons.Rounded.WbSunny, tr("Свет", "Light"), Modifier.weight(1f), marked = pad == "light")
      Key("minus", Icons.Rounded.Remove, "−", Modifier.weight(1f))
      Key("plus", Icons.Rounded.Add, "+", Modifier.weight(1f))
      Key("sound", Icons.Rounded.MusicNote, tr("Звук", "Sound"), Modifier.weight(1f), marked = pad == "sound")
    }
  }
}

// ---------------------------------------------------------------- сеть и связь
@Composable
private fun NetworkPage(s: WulState, vm: MainViewModel, onBack: () -> Unit) {
  val c = LocalWul.current
  val nets by vm.wifi.collectAsState()
  val scanning by vm.wifiScanning.collectAsState()
  var askLink by remember { mutableStateOf<String?>(null) }
  var pick by remember { mutableStateOf<WifiNet?>(null) }
  val ble = s.net.link == "ble"
  LaunchedEffect(Unit) { if (!ble && nets == null) vm.scanWifi() }
  TabList {
    item { SubTitle(tr("Сеть и связь", "Network & connection"), onBack) }
    item {
      WCard(Modifier.reveal(0)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          IconBadge(if (ble) Icons.Rounded.Bluetooth else Icons.Rounded.Wifi, c.dusk, 48)
          Spacer(Modifier.width(14.dp))
          Column(Modifier.weight(1f)) {
            Text(if (ble) s.net.ble.ifBlank { "Bluetooth" } else s.net.ssid.ifBlank { "—" }, color = c.text, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
            Muted(if (ble) tr("связь по Bluetooth LE", "connected over Bluetooth LE") else "IP ${s.net.ip.ifBlank { "—" }} · ${s.net.rssi} dBm", size = 13)
          }
          if (!ble) SignalBars(rssiLevel(s.net.rssi), c.ok)
        }
      }
    }
    item { SectionTitle(tr("Способ связи с телефоном", "How the phone connects")) }
    item {
      Row(Modifier.fillMaxWidth().reveal(1), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        LinkOption(Icons.Rounded.Wifi, "Wi-Fi", tr("через домашнюю сеть, время из интернета", "via home network, time from the internet"),
          !ble, Modifier.weight(1f)) { if (ble) askLink = "wifi" }
        LinkOption(Icons.Rounded.Bluetooth, "Bluetooth", tr("напрямую, без роутера; время даёт телефон", "direct, no router; the phone gives the time"),
          ble, Modifier.weight(1f)) { if (!ble) askLink = "ble" }
      }
    }
    if (!ble) {
      item {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
          SectionTitle(tr("Wi-Fi сети", "Wi-Fi networks"))
          Spacer(Modifier.weight(1f))
          if (scanning) CircularProgressIndicator(color = c.dawn, strokeWidth = 2.dp, modifier = Modifier.size(18.dp).padding(end = 4.dp))
          else TextButton(onClick = { vm.scanWifi() }) {
            Icon(Icons.Rounded.WifiFind, null, tint = c.dawn, modifier = Modifier.size(18.dp)); Spacer(Modifier.width(4.dp))
            Text(tr("Обновить", "Refresh"), color = c.dawn)
          }
        }
      }
      val list = nets.orEmpty()
      if (list.isEmpty() && !scanning) item { WCard { Muted(tr("Нажмите «Обновить», чтобы найти сети.", "Tap Refresh to find networks.")) } }
      items(list, key = { it.ssid }) { n ->
        val current = n.ssid == s.net.ssid
        Row(
          Modifier.fillMaxWidth().animateItem().clip(CtlShape).background(if (current) c.dawn.copy(alpha = .12f) else c.surface)
            .bouncy { if (!current) pick = n }.heightIn(min = 56.dp).padding(horizontal = 14.dp),
          verticalAlignment = Alignment.CenterVertically,
        ) {
          SignalBars(rssiLevel(n.rssi), if (current) c.dawn else c.text)
          Spacer(Modifier.width(14.dp))
          Text(n.ssid, color = c.text, modifier = Modifier.weight(1f), maxLines = 1)
          if (current) Icon(Icons.Rounded.CheckCircle, tr("Текущая", "Current"), tint = c.dawn, modifier = Modifier.size(20.dp))
          else if (n.secure) Icon(Icons.Rounded.Lock, null, tint = c.muted, modifier = Modifier.size(18.dp))
        }
      }
    } else item {
      WCard { Muted(tr("В режиме Bluetooth Wi-Fi модуля выключен. Чтобы сменить сеть, переключитесь на Wi-Fi.",
        "In Bluetooth mode the module’s Wi-Fi is off. Switch to Wi-Fi to change the network.")) }
    }
  }

  askLink?.let { l ->
    AlertDialog(
      onDismissRequest = { askLink = null },
      icon = { Icon(if (l == "ble") Icons.Rounded.Bluetooth else Icons.Rounded.Wifi, null, tint = c.dusk) },
      title = { Text(if (l == "ble") tr("Перейти на Bluetooth?", "Switch to Bluetooth?") else tr("Перейти на Wi-Fi?", "Switch to Wi-Fi?")) },
      text = { Text(if (l == "ble") tr("Модуль перезагрузится и будет доступен только по Bluetooth (WakeUpLight-XXXX). Веб-панель по Wi-Fi работать не будет.",
        "The module restarts and is reachable only over Bluetooth (WakeUpLight-XXXX). The Wi-Fi web panel won’t work.")
        else tr("Модуль перезагрузится и подключится к сохранённой Wi-Fi сети.", "The module restarts and joins the saved Wi-Fi network.")) },
      confirmButton = { TextButton(onClick = { askLink = null; vm.network(null, null, l) }) { Text(tr("Перейти", "Switch"), color = c.dawn) } },
      dismissButton = { TextButton(onClick = { askLink = null }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
      containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
    )
  }

  pick?.let { n ->
    var pass by remember(n.ssid) { mutableStateOf("") }
    AlertDialog(
      onDismissRequest = { pick = null },
      icon = { Icon(Icons.Rounded.Wifi, null, tint = c.dawn) },
      title = { Text(n.ssid) },
      text = {
        Column {
          Muted(tr("Модуль перезагрузится и подключится к этой сети.", "The module will restart and join this network."))
          if (n.secure) OutlinedTextField(pass, { pass = it }, label = { Text(tr("Пароль", "Password")) }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(), colors = fieldColors(), shape = CtlShape,
            modifier = Modifier.fillMaxWidth().padding(top = 12.dp))
        }
      },
      confirmButton = {
        TextButton(enabled = !n.secure || pass.length >= 8, onClick = { pick = null; vm.network(n.ssid, pass, "wifi") }) {
          Text(tr("Подключить", "Connect"), color = c.dawn)
        }
      },
      dismissButton = { TextButton(onClick = { pick = null }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
      containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
    )
  }
}

@Composable
private fun LinkOption(icon: ImageVector, title: String, sub: String, on: Boolean, modifier: Modifier, onClick: () -> Unit) {
  val c = LocalWul.current
  val bd by animateColorAsState(if (on) c.dusk else c.border, tween(220), label = "lb")
  Column(
    modifier.clip(CardShape).background(if (on) c.dusk.copy(alpha = .12f) else c.surface).border(2.dp, bd, CardShape)
      .bouncy(Role.RadioButton, onClick = onClick).padding(14.dp),
  ) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      IconBadge(icon, c.dusk, 36)
      Spacer(Modifier.weight(1f))
      AnimatedVisibility(on, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
        Icon(Icons.Rounded.CheckCircle, null, tint = c.dusk)
      }
    }
    Spacer(Modifier.height(10.dp))
    Text(title, color = c.text, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
    Muted(sub, size = 12)
  }
}

// ---------------------------------------------------------------- язык и регион
@Composable
private fun LanguagePage(s: WulState, vm: MainViewModel, onBack: () -> Unit) {
  val c = LocalWul.current
  TabList {
    item { SubTitle(tr("Язык и регион", "Language & region"), onBack) }
    item { SectionTitle(tr("Язык", "Language")) }
    item {
      WCard(Modifier.reveal(0)) {
        Pills(listOf("Русский", "English"), { (it == 1) == (s.settings.lang == "en") }) {
          vm.settings(JSONObject().put("lang", if (it == 1) "en" else "ru"))
        }
        Muted(tr("Язык приложения, веб-панели и модуля.", "Language of the app, the web panel and the module."), Modifier.padding(top = 8.dp), 13)
      }
    }
    item { SectionTitle(tr("Часовой пояс", "Time zone")) }
    items(REGIONS, key = { it.id }) { r ->
      val on = r.id == s.settings.region
      val bg by animateColorAsState(if (on) c.dawn.copy(alpha = .14f) else c.surface, tween(200), label = "rg")
      Row(
        Modifier.fillMaxWidth().clip(CtlShape).background(bg)
          .bouncy(Role.RadioButton) { if (!on) vm.settings(JSONObject().put("region", r.id).put("tz", r.tz)) }
          .heightIn(min = 52.dp).padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
      ) {
        Icon(Icons.Rounded.Public, null, tint = if (on) c.dawn else c.muted, modifier = Modifier.size(20.dp))
        Spacer(Modifier.width(12.dp))
        Text(if (isEn()) r.en else r.ru, color = c.text, modifier = Modifier.weight(1f))
        if (on) Box(Modifier.size(10.dp).clip(CircleShape).background(c.dawn))
      }
    }
  }
}
