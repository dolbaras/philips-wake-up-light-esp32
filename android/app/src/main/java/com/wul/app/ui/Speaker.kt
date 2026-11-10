package com.wul.app.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeDown
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Alarm
import androidx.compose.material.icons.rounded.BrightnessHigh
import androidx.compose.material.icons.rounded.BrightnessLow
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Lightbulb
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.Speaker
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wul.app.MainViewModel
import com.wul.app.data.WulState
import com.wul.app.data.hasSppPermissions
import com.wul.app.data.sppPermissions

/**
 * Экран режима колонки: модуль без Wi-Fi, связь по Bluetooth SPP. Что играет, громкость, светомузыка, лампа,
 * ближайший будильник и главное действие — «Вернуть Wi-Fi».
 */
@Composable
fun SpeakerScreen(s: WulState, vm: MainViewModel) {
  val c = LocalWul.current
  var confirm by remember { mutableStateOf(false) }
  val sp = s.speaker
  TabList {
    item { Title(tr("Колонка", "Speaker"), tr("часы играют музыку с телефона · связь по Bluetooth", "the clock plays music from your phone · Bluetooth link")) }
    item {
      WCard(Modifier.reveal(0)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Box(Modifier.size(56.dp).clip(RoundedCornerShape(18.dp)).background(c.dusk.copy(alpha = .16f)), contentAlignment = Alignment.Center) {
            if (sp.playing) Equalizer(c.dusk, Modifier.size(28.dp)) else Icon(Icons.Rounded.MusicNote, null, tint = c.dusk, modifier = Modifier.size(28.dp))
          }
          Spacer(Modifier.width(14.dp))
          Column(Modifier.weight(1f)) {
            Text(
              when {
                sp.title.isNotBlank() -> sp.title
                sp.playing -> tr("Играет музыка", "Music is playing")
                sp.connected -> tr("Пауза", "Paused")
                else -> tr("Телефон не подключён", "No phone connected")
              },
              fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = c.text, maxLines = 2, overflow = TextOverflow.Ellipsis,
            )
            val sub = listOf(sp.artist, sp.peer.ifBlank { if (sp.connected) "" else tr("подключите телефон к «${sp.name}»", "connect your phone to “${sp.name}”") })
              .filter { it.isNotBlank() }.joinToString(" · ")
            if (sub.isNotBlank()) Muted(sub, size = 13)
          }
        }
        Spacer(Modifier.height(12.dp))
        SliderRow(tr("Громкость часов", "Clock volume"), s.volume, 0..20, accent = c.dusk,
          startIcon = Icons.AutoMirrored.Rounded.VolumeDown, endIcon = Icons.AutoMirrored.Rounded.VolumeUp) { vm.volume(it) }
        Muted(tr("связана с громкостью телефона", "linked to the phone volume"), size = 12)
      }
    }
    if (s.eq.available) {
      item { SectionTitle(tr("Звучание", "Sound")) }
      item { EqCard(s, vm, listen = false, Modifier.reveal(1)) }
    }
    item { SectionTitle(tr("Светомузыка", "Light show")) }
    item {
      WCard(Modifier.reveal(1), pad = 12) {
        SwitchRow(tr("Эквалайзер на табло", "Equalizer on the display"), sp.showEq,
          tr("цифры прыгают в такт", "digits bounce to the music"), c.dusk, Icons.Rounded.GraphicEq) { vm.speakerLights(showEq = it) }
        SwitchRow(tr("Лампа в такт", "Lamp to the beat"), sp.lamp,
          tr("яркость следует за громкостью", "brightness follows loudness"), c.dawn, Icons.Rounded.Lightbulb) { vm.speakerLights(lamp = it) }
      }
    }
    item { SectionTitle(tr("Лампа", "Lamp")) }
    item {
      WCard(Modifier.reveal(2), pad = 12) {
        SwitchRow(tr("Лампа", "Lamp"), s.clock.lightOn,
          if (s.clock.lightOn) tr("горит, яркость ", "on, brightness ") + "${s.clock.lightLevel} / 20" else tr("выключена", "off"),
          c.dawn, Icons.Rounded.Lightbulb) { vm.light(on = it) }
        AnimatedVisibility(!(sp.lamp && sp.playing), enter = fadeIn(), exit = fadeOut()) {
          SliderRow(tr("Яркость лампы", "Lamp brightness"), s.clock.lightLevel, 1..20,
            startIcon = Icons.Rounded.BrightnessLow, endIcon = Icons.Rounded.BrightnessHigh) { vm.light(on = true, level = it) }
        }
      }
    }
    item {
      WCard(Modifier.reveal(3), pad = 12) {
        Row(Modifier.padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
          IconBadge(Icons.Rounded.Alarm, c.dawn, 36); Spacer(Modifier.width(12.dp))
          Column {
            Text(tr("Ближайший будильник", "Next alarm"), fontSize = 16.sp, color = c.text)
            Muted(s.next?.let { hm(it.time) + tr(" · музыка встанет на паузу", " · the music will pause") } ?: tr("будильники выключены", "alarms are off"), size = 13)
          }
        }
      }
    }
    item {
      Column(Modifier.padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        GradientButton(tr("Вернуть Wi-Fi", "Back to Wi-Fi"), c.dawnBrush, c.onDawn, icon = Icons.Rounded.Wifi) { confirm = true }
        Muted(tr("Модуль перезагрузится, музыка остановится, приложение само переключится на Wi-Fi. То же — «Звук» 3 раза на часах.",
          "The module restarts, the music stops and the app switches back to Wi-Fi. Same as pressing Sound 3 times on the clock."),
          Modifier.fillMaxWidth().padding(horizontal = 4.dp), size = 12)
      }
    }
  }
  if (confirm) AlertDialog(
    onDismissRequest = { confirm = false },
    icon = { Icon(Icons.Rounded.Speaker, null, tint = c.dawn) },
    title = { Text(tr("Выключить колонку?", "Turn off the speaker?")) },
    text = { Text(tr("Музыка остановится, модуль перезагрузится в Wi-Fi (около 10 секунд).",
      "The music stops and the module restarts into Wi-Fi (about 10 seconds).")) },
    confirmButton = { TextButton(onClick = { confirm = false; vm.speakerMode(false) }) { Text(tr("Вернуть Wi-Fi", "Back to Wi-Fi"), color = c.dawn) } },
    dismissButton = { TextButton(onClick = { confirm = false }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
    containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
  )
}

/**
 * «Часы в режиме колонки» — для экранов подключения: если колонку включили кнопками на часах, приложению может
 * не хватать разрешения Bluetooth (без него оно часы-колонку не видит). Спросить и сразу поискать.
 */
@Composable
fun SpeakerFindButton(vm: MainViewModel, modifier: Modifier = Modifier) {
  val c = LocalWul.current
  val ctx = LocalContext.current
  val need = tr("Без разрешения «Устройства поблизости» приложение не видит часы-колонку", "Without the “Nearby devices” permission the app can't see the clock speaker")
  val perms = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { r ->
    if (r.values.all { it }) vm.findSpeaker() else vm.say(need)
  }
  GhostButton(tr("Часы в режиме колонки", "The clock is in speaker mode"), c.text, modifier, icon = Icons.Rounded.Speaker) {
    if (hasSppPermissions(ctx)) vm.findSpeaker() else perms.launch(sppPermissions())
  }
}
