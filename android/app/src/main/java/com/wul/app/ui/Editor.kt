package com.wul.app.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Label
import androidx.compose.material.icons.automirrored.rounded.VolumeDown
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.Repeat
import androidx.compose.material.icons.rounded.Snooze
import androidx.compose.material.icons.rounded.WbSunny
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.TimePickerDefaults
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wul.app.MainViewModel
import com.wul.app.data.Alarm
import kotlinx.coroutines.launch

/**
 * Редактор будильника — свой нижний лист внутри окна (без системного ModalBottomSheet и его серой
 * полосы навигации): затемнение, выезд снизу, закрытие свайпом вниз за шапку или кнопкой «назад».
 */
@Composable
fun AlarmSheet(open: Boolean, initial: Alarm?, session: Int, vm: MainViewModel, onClose: () -> Unit) {
  BackHandler(enabled = open, onBack = onClose)
  AnimatedVisibility(open, enter = fadeIn(tween(250)), exit = fadeOut(tween(220))) {
    Box(Modifier.fillMaxSize().background(LocalWul.current.scrim)
      .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onClose))
  }
  AnimatedVisibility(
    open,
    enter = slideInVertically(spring(Spring.DampingRatioLowBouncy, Spring.StiffnessMediumLow)) { it },
    exit = slideOutVertically(tween(240)) { it },
  ) {
    key(session) { EditorContent(initial, vm, onClose) }
  }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun EditorContent(initial: Alarm?, vm: MainViewModel, onClose: () -> Unit) {
  val c = LocalWul.current
  val scope = rememberCoroutineScope()
  val density = LocalDensity.current
  var a by remember { mutableStateOf(initial ?: Alarm()) }
  var previewing by remember { mutableIntStateOf(-1) }
  var saving by remember { mutableStateOf(false) }
  var confirmDelete by remember { mutableStateOf(false) }
  var pickTime by remember { mutableStateOf(false) }
  val drag = remember { Animatable(0f) }
  val defaultLabel = tr("Будильник", "Alarm")

  fun close() { if (previewing >= 0) vm.previewStop(); onClose() }

  Column(Modifier.fillMaxSize().statusBarsPadding().padding(top = 12.dp)) {
    Column(
      Modifier.fillMaxSize()
        .graphicsLayer { translationY = drag.value }
        .clip(RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp))
        .background(c.bg)
        .clickable(remember { MutableInteractionSource() }, indication = null) {},   // не пропускать касания к затемнению
    ) {
      // шапка: ручка + действия; за неё лист тянется вниз
      Column(
        Modifier.fillMaxWidth().pointerInput(Unit) {
          detectVerticalDragGestures(
            onDragEnd = {
              if (drag.value > with(density) { 140.dp.toPx() }) close()
              else scope.launch { drag.animateTo(0f, spring(Spring.DampingRatioMediumBouncy)) }
            },
          ) { ch, dy -> ch.consume(); scope.launch { drag.snapTo((drag.value + dy).coerceAtLeast(0f)) } }
        },
      ) {
        Box(Modifier.fillMaxWidth().padding(top = 10.dp), contentAlignment = Alignment.Center) {
          Box(Modifier.size(40.dp, 5.dp).clip(RoundedCornerShape(50)).background(c.border))
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
          IconButton(onClick = ::close) { Icon(Icons.Rounded.Close, tr("Отмена", "Cancel"), tint = c.text) }
          Text(if (initial == null) tr("Новый будильник", "New alarm") else tr("Будильник", "Alarm"), fontSize = 18.sp,
            fontWeight = FontWeight.SemiBold, color = c.text, modifier = Modifier.weight(1f))
          Box(
            Modifier.heightIn(min = 44.dp).clip(RoundedCornerShape(50)).background(c.dawnBrush)
              .bouncy(enabled = !saving) {
                saving = true
                vm.saveAlarm(a.copy(label = a.label.ifBlank { defaultLabel })) { close() }.invokeOnCompletion { saving = false }
              }.padding(horizontal = 18.dp),
            contentAlignment = Alignment.Center,
          ) { Text(if (saving) tr("Сохраняю…", "Saving…") else tr("Сохранить", "Save"), color = c.onAccent, fontWeight = FontWeight.SemiBold) }
        }
      }

      Column(
        Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).imePadding()
          .padding(horizontal = 16.dp).navigationBarsPadding(),
        verticalArrangement = Arrangement.spacedBy(12.dp),
      ) {
        // время — крупно, по нажатию циферблат
        Box(
          Modifier.fillMaxWidth().clip(CardShape).background(c.surface).bouncy { pickTime = true }.padding(vertical = 14.dp),
          contentAlignment = Alignment.Center,
        ) {
          Column(horizontalAlignment = Alignment.CenterHorizontally) {
            FlipText("${pad(a.hour)}:${pad(a.minute)}", TextStyle(fontSize = 76.sp, fontWeight = FontWeight.Bold, fontFeatureSettings = "tnum"), c.text)
            Row(verticalAlignment = Alignment.CenterVertically) {
              Icon(Icons.Rounded.Edit, null, tint = c.muted, modifier = Modifier.size(14.dp)); Spacer(Modifier.width(4.dp))
              Muted(tr("нажмите, чтобы изменить", "tap to change"), size = 13)
            }
          }
        }

        WCard {
          Row(verticalAlignment = Alignment.CenterVertically) {
            IconBadge(Icons.Rounded.Repeat, c.dawn, 36); Spacer(Modifier.width(12.dp))
            Text(tr("Повтор", "Repeat"), fontSize = 16.sp, color = c.text, modifier = Modifier.weight(1f))
            Muted(daysText(a.days))
          }
          Spacer(Modifier.height(12.dp))
          Pills(days(), { (a.days shr it) and 1 == 1 }) { a = a.copy(days = a.days xor (1 shl it)) }
          Spacer(Modifier.height(8.dp))
          val presets = listOf(0 to tr("Один раз", "Once"), 31 to tr("Будни", "Weekdays"), 96 to tr("Выходные", "Weekend"), 127 to tr("Все дни", "Daily"))
          Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            presets.forEach { (m, t) ->
              val on = a.days == m
              Text(t, fontSize = 13.sp, color = if (on) c.dawn else c.muted, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal,
                modifier = Modifier.clip(RoundedCornerShape(50)).background(if (on) c.dawn.copy(alpha = .14f) else Color.Transparent)
                  .bouncy { a = a.copy(days = m) }.padding(horizontal = 10.dp, vertical = 8.dp))
            }
          }
          OutlinedTextField(a.label, { a = a.copy(label = it.take(23)) }, label = { Text(tr("Название", "Label")) }, singleLine = true,
            leadingIcon = { Icon(Icons.AutoMirrored.Rounded.Label, null) }, placeholder = { Text(defaultLabel) },
            colors = fieldColors(), shape = CtlShape, modifier = Modifier.fillMaxWidth().padding(top = 12.dp))
        }

        SectionTitle(tr("Свет", "Light"))
        WCard {
          SwitchRow(tr("Рассвет", "Sunrise"), a.light, tr("лампа разгорается до будильника", "the lamp brightens before the alarm"),
            c.dawn, Icons.Rounded.WbSunny) { a = a.copy(light = it) }
          AnimatedVisibility(a.light, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Column {
              Spacer(Modifier.height(10.dp))
              SunriseCurve(a.sunriseMin, a.intensity, "${pad(a.hour)}:${pad(a.minute)}")
              Row(Modifier.padding(top = 14.dp, bottom = 8.dp)) {
                Text(tr("Длительность", "Duration"), fontSize = 16.sp, color = c.text, modifier = Modifier.weight(1f))
                Muted(minutes(a.sunriseMin))
              }
              val mins = listOf(20, 25, 30, 35, 40)
              Pills(mins.map(Int::toString), { mins[it] == a.sunriseMin }) { a = a.copy(sunriseMin = mins[it]) }
              SliderRow(tr("Яркость", "Brightness"), a.intensity, 1..20,
                brush = Brush.horizontalGradient(listOf(dawnColor(.15f), dawnColor(.55f), dawnColor(.9f))),
                onLive = { a = a.copy(intensity = it) }) { a = a.copy(intensity = it) }
              Muted(tr("К моменту будильника свет достигает этой яркости.", "The light reaches this brightness at alarm time."), size = 13)
            }
          }
        }

        SectionTitle(tr("Звук", "Sound"))
        WCard {
          SwitchRow(tr("Звук", "Sound"), a.sound, tr("плавно нарастает в момент будильника", "fades in at alarm time"),
            c.dawn, Icons.Rounded.MusicNote) { a = a.copy(sound = it) }
          AnimatedVisibility(a.sound, enter = fadeIn() + expandVertically(), exit = fadeOut() + shrinkVertically()) {
            Column {
              Spacer(Modifier.height(8.dp))
              SoundList(a.soundIdx, previewing, c.dawn, onPick = { a = a.copy(soundIdx = it) }) { i ->
                if (previewing == i) { previewing = -1; vm.previewStop() } else { previewing = i; vm.preview(i, a.volume) }
              }
              SliderRow(tr("Громкость", "Volume"), a.volume, 0..20,
                startIcon = Icons.AutoMirrored.Rounded.VolumeDown, endIcon = Icons.AutoMirrored.Rounded.VolumeUp) { a = a.copy(volume = it) }
            }
          }
          val mn = tr(" мин", " min")
          SliderRow(tr("Повтор через", "Snooze for"), a.snoozeMin, 1..20, { "$it$mn" }, startIcon = Icons.Rounded.Snooze) { a = a.copy(snoozeMin = it) }
        }

        if (initial != null) GhostButton(tr("Удалить будильник", "Delete alarm"), c.danger, icon = Icons.Rounded.DeleteOutline) { confirmDelete = true }
        Spacer(Modifier.height(24.dp))
      }
    }
  }

  if (pickTime) {
    val dial = rememberTimePickerState(a.hour, a.minute, is24Hour = true)
    AlertDialog(
      onDismissRequest = { pickTime = false },
      confirmButton = { TextButton(onClick = { a = a.copy(hour = dial.hour, minute = dial.minute); pickTime = false }) { Text(tr("Готово", "Done"), color = c.dawn) } },
      dismissButton = { TextButton(onClick = { pickTime = false }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
      text = {
        TimePicker(dial, colors = TimePickerDefaults.colors(
          clockDialColor = c.surface2, selectorColor = c.dawn, clockDialSelectedContentColor = c.onAccent,
          clockDialUnselectedContentColor = c.text, timeSelectorSelectedContainerColor = c.dawn,
          timeSelectorSelectedContentColor = c.onAccent,
          timeSelectorUnselectedContainerColor = c.surface2, timeSelectorUnselectedContentColor = c.text))
      },
      containerColor = c.surface,
    )
  }

  if (confirmDelete && initial != null) AlertDialog(
    onDismissRequest = { confirmDelete = false },
    icon = { Icon(Icons.Rounded.DeleteOutline, null, tint = c.danger) },
    title = { Text(tr("Удалить будильник?", "Delete alarm?")) },
    text = { Text("${pad(initial.hour)}:${pad(initial.minute)} · ${initial.label}") },
    confirmButton = { TextButton(onClick = { confirmDelete = false; vm.deleteAlarm(initial.id) { close() } }) { Text(tr("Удалить", "Delete"), color = c.danger) } },
    dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text(tr("Отмена", "Cancel"), color = c.text) } },
    containerColor = c.surface, titleContentColor = c.text, textContentColor = c.muted,
  )
}
