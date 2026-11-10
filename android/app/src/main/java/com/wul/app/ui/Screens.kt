package com.wul.app.ui

import android.annotation.SuppressLint
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.BluetoothSearching
import androidx.compose.material.icons.automirrored.rounded.VolumeDown
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Bedtime
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.EditNote
import androidx.compose.material.icons.rounded.Insights
import androidx.compose.material.icons.rounded.Key
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.NightsStay
import androidx.compose.material.icons.rounded.NotificationsActive
import androidx.compose.material.icons.rounded.NotificationsNone
import androidx.compose.material.icons.rounded.PhoneIphone
import androidx.compose.material.icons.rounded.OpenInBrowser
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Router
import androidx.compose.material.icons.rounded.Snooze
import androidx.compose.material.icons.rounded.Timer
import androidx.compose.material.icons.rounded.WbSunny
import androidx.compose.material.icons.rounded.WbTwilight
import androidx.compose.material.icons.rounded.Wifi
import androidx.compose.material.icons.rounded.WifiFind
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wul.app.Link
import com.wul.app.MainViewModel
import com.wul.app.data.Alarm
import com.wul.app.data.BleDevice
import com.wul.app.data.Mode
import com.wul.app.data.SleepEntry
import com.wul.app.data.WulState
import com.wul.app.data.blePermissions
import com.wul.app.data.hasBlePermissions
import org.json.JSONObject

private val ListPad = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 28.dp)
private val Gap = Arrangement.spacedBy(12.dp)

@Composable
fun TabList(content: LazyListScope.() -> Unit) =
  LazyColumn(Modifier.fillMaxSize(), contentPadding = ListPad, verticalArrangement = Gap, content = content)

@Composable
fun Title(text: String, sub: String? = null) {
  Column(Modifier.padding(top = 6.dp, start = 4.dp, bottom = 2.dp)) {
    Text(text, fontSize = 30.sp, fontWeight = FontWeight.Bold, color = LocalWul.current.text)
    if (sub != null) Muted(sub, size = 15)
  }
}

// ---------------------------------------------------------------- Будильник
/** В доке iPhone/iPad: модуль отпустил часы (работают «по-старому»), будильники модуля в это время не звенят. */
@Composable
fun DockedCard(s: WulState) {
  val c = LocalWul.current
  WCard(Modifier.reveal(0)) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      IconBadge(Icons.Rounded.PhoneIphone, c.dusk, 44)
      Spacer(Modifier.width(12.dp))
      Column(Modifier.weight(1f)) {
        Text(if (s.device.dock == "device") tr("В доке iPhone/iPad", "iPhone/iPad in the dock") else tr("Модуль отпустил часы", "The module released the clock"),
          fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = c.text)
        Muted(if (s.device.dock == "device")
          tr("Часами управляет его приложение Philips. Будильники модуля не прозвенят, пока он в доке; выньте — модуль вернётся сам за ~10 с.",
            "Its Philips app controls the clock. The module's alarms won't ring while it is docked; take it out and the module is back in ~10 s.")
        else tr("Пассивный режим включён вручную через API (диагностика).",
            "Passive mode was turned on manually via the API (diagnostics)."), size = 13)
      }
    }
  }
}

@Composable
fun WakeTab(s: WulState, now: Long, vm: MainViewModel, onEdit: (Alarm?) -> Unit) {
  val c = LocalWul.current
  val tooMany = tr("Не больше 10 будильников", "No more than 10 alarms")
  TabList {
    item { Title(tr("Будильник", "Wake up")) }
    if (s.device.docked) item { DockedCard(s) }
    item { NextAlarmHero(s, now) }
    item {
      Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        SectionTitle(tr("Будильники", "Alarms"))
        Spacer(Modifier.weight(1f))
        Row(
          Modifier.heightIn(min = 44.dp).clip(RoundedCornerShape(50)).background(c.dawnBrush)
            .bouncy { if (s.alarms.size >= 10) vm.say(tooMany) else onEdit(null) }
            .padding(horizontal = 16.dp),
          verticalAlignment = Alignment.CenterVertically,
        ) {
          Icon(Icons.Rounded.Add, null, tint = c.onAccent, modifier = Modifier.size(20.dp))
          Spacer(Modifier.width(6.dp))
          Text(tr("Добавить", "Add"), color = c.onAccent, fontWeight = FontWeight.SemiBold)
        }
      }
    }
    if (s.alarms.isEmpty()) item {
      WCard(Modifier.reveal(1)) {
        Column(Modifier.fillMaxWidth().padding(vertical = 18.dp), horizontalAlignment = Alignment.CenterHorizontally) {
          IconBadge(Icons.Rounded.NotificationsNone, c.dawn, 56)
          Spacer(Modifier.height(12.dp))
          Text(tr("Будильников пока нет", "No alarms yet"), fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = c.text)
          Muted(tr("Добавьте первый — рассвет начнётся сам.", "Add one — the sunrise will start on its own."),
            Modifier.padding(top = 4.dp))
        }
      }
    }
    itemsIndexed(s.alarms.sortedBy { it.hour * 60 + it.minute }, key = { _, a -> a.id }) { i, a ->
      AlarmCard(a, s.next?.id == a.id, vm, onEdit, Modifier.animateItem().reveal(i + 1))
    }
  }
}

@Composable
private fun NextAlarmHero(s: WulState, now: Long) {
  val c = LocalWul.current
  val a = s.next?.let { n -> s.alarms.firstOrNull { it.id == n.id } }
  val on = c.onAccent
  Box(Modifier.fillMaxWidth().reveal(0).clip(CardShape).background(c.dawnBrush)) {
    Waves(Color.White, Modifier.matchParentSize())
    SunGlow(Modifier.size(150.dp).align(Alignment.TopEnd).offset(x = 28.dp, y = (-24).dp))
    Column(Modifier.padding(22.dp)) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Rounded.NotificationsActive, null, tint = on.copy(alpha = .85f), modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(6.dp))
        Text(a?.label?.ifBlank { null } ?: tr("Ближайший будильник", "Next alarm"), color = on.copy(alpha = .85f), fontSize = 15.sp)
      }
      FlipText(s.next?.let { hm(it.time) } ?: "—:—", TextStyle(fontSize = 60.sp, fontWeight = FontWeight.Bold, fontFeatureSettings = "tnum"), on)
      Text(
        if (s.next == null) tr("Будильники выключены", "Alarms are off")
        else tr("через ", "in ") + dur(s.next.time - now) +
          if (a != null && a.light) " · " + tr("рассвет", "sunrise") + " " + minutes(a.sunriseMin) else "",
        color = on.copy(alpha = .9f), fontSize = 15.sp,
      )
    }
  }
}

@Composable
private fun AlarmCard(a: Alarm, isNext: Boolean, vm: MainViewModel, onEdit: (Alarm?) -> Unit, modifier: Modifier) {
  val c = LocalWul.current
  WCard(modifier, onClick = { onEdit(a) }) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      Column(Modifier.weight(1f)) {
        val alpha = if (a.enabled) 1f else .45f
        Row(verticalAlignment = Alignment.CenterVertically) {
          Text("${pad(a.hour)}:${pad(a.minute)}", fontSize = 40.sp, fontWeight = FontWeight.SemiBold, style = Num,
            color = c.text.copy(alpha = alpha))
          if (isNext && a.enabled) {
            Spacer(Modifier.width(10.dp))
            Text(tr("скоро", "next"), fontSize = 12.sp, fontWeight = FontWeight.Bold, color = c.onAccent,
              modifier = Modifier.clip(RoundedCornerShape(50)).background(c.dawn).padding(horizontal = 8.dp, vertical = 3.dp))
          }
        }
        Muted("${a.label.ifBlank { tr("Будильник", "Alarm") }} · ${daysText(a.days)}")
        ChipRow {
          if (a.light) Chip(Icons.Rounded.WbSunny, "${minutes(a.sunriseMin)} · ${a.intensity}", c.dawn)
          if (a.sound) Chip(Icons.Rounded.MusicNote, soundName(a.soundIdx), c.dawn)
        }
      }
      Switch(a.enabled, onCheckedChange = { vm.enableAlarm(a.id, it) }, colors = SwitchDefaults.colors(
        checkedTrackColor = c.dawn, checkedThumbColor = c.onAccent, checkedBorderColor = c.dawn,
        uncheckedTrackColor = c.surface2, uncheckedBorderColor = c.border, uncheckedThumbColor = c.muted))
    }
  }
}

// ---------------------------------------------------------------- Сон (закат)
@Composable
fun SleepTab(s: WulState, vm: MainViewModel, previewing: Int, setPreviewing: (Int) -> Unit) {
  val c = LocalWul.current
  val mn = tr(" мин", " min")
  TabList {
    item { Title(tr("Засыпание", "Fall asleep")) }
    item {
      Box(Modifier.fillMaxWidth().reveal(0).clip(CardShape).background(c.duskBrush)) {
        Waves(Color(0xFFA78BFA), Modifier.matchParentSize())
        MoonGlow(Modifier.size(140.dp).align(Alignment.TopEnd).offset(x = 20.dp, y = (-10).dp))
        Column(Modifier.padding(22.dp)) {
          Text(tr("Закат", "Sunset"), color = Color.White.copy(alpha = .8f), fontSize = 15.sp)
          Text(minutes(s.dusk.minutes), color = Color.White, fontSize = 44.sp, fontWeight = FontWeight.Bold, style = Num)
          Text(if (s.dusk.music) tr("под «", "with “") + soundName(s.dusk.musicIdx) + tr("»", "”") else tr("без музыки", "no music"),
            color = Color.White.copy(alpha = .85f), fontSize = 15.sp)
          Spacer(Modifier.height(18.dp))
          GradientButton(tr("Начать закат", "Start sunset"), Brush.linearGradient(listOf(Color.White, Color(0xFFEDE9FE))),
            Color(0xFF312E81), icon = Icons.Rounded.PlayArrow, enabled = s.mode == Mode.Idle) { vm.duskStart() }
        }
      }
    }
    item {
      WCard(Modifier.reveal(1)) {
        SliderRow(tr("Длительность", "Duration"), s.dusk.minutes, 5..60, { "$it$mn" }, c.dusk, step = 5,
          brush = c.duskBrush, startIcon = Icons.Rounded.Timer) { vm.dusk(JSONObject().put("minutes", it)) }
        SwitchRow(tr("Музыка", "Music"), s.dusk.music, tr("затихает вместе со светом", "fades out with the light"), c.dusk,
          Icons.Rounded.MusicNote) { vm.dusk(JSONObject().put("music", it)) }
        if (s.dusk.music) SliderRow(tr("Громкость", "Volume"), s.dusk.volume, 0..20, accent = c.dusk, brush = c.duskBrush,
          startIcon = Icons.AutoMirrored.Rounded.VolumeDown, endIcon = Icons.AutoMirrored.Rounded.VolumeUp) { vm.dusk(JSONObject().put("volume", it)) }
      }
    }
    if (s.dusk.music) item {
      Column(Modifier.reveal(2)) {
        SectionTitle(tr("Мелодия", "Melody"))
        Spacer(Modifier.height(6.dp))
        SoundList(s.dusk.musicIdx, previewing, c.dusk, onPick = { vm.dusk(JSONObject().put("musicIdx", it)) }) { i ->
          if (previewing == i) { setPreviewing(-1); vm.previewStop() } else { setPreviewing(i); vm.preview(i, s.dusk.volume) }
        }
      }
    }
  }
}

// ---------------------------------------------------------------- Статистика
@Composable
fun StatsTab(s: WulState, vm: MainViewModel, log: List<SleepEntry>) {
  val c = LocalWul.current
  LaunchedEffect(Unit) { vm.loadLog() }
  val nights = log.filter { it.start > 0 && it.end > 0 }.takeLast(14)
  TabList {
    item { Title(tr("Статистика", "Stats")) }
    if (s.question) item { MorningQuestion(vm) }
    item {
      val avgSleep = nights.takeIf { it.isNotEmpty() }?.map { it.end - it.start }?.average()?.toLong()
      // отбой сдвигаем на 12 ч, чтобы полночь не ломала среднее
      val bed = nights.takeIf { it.isNotEmpty() }?.map { ((it.start + 12 * 3600) % 86400) }?.average()?.toLong()
      val wake = nights.takeIf { it.isNotEmpty() }?.map { it.end % 86400 }?.average()?.toLong()
      Row(Modifier.fillMaxWidth().reveal(1), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Stat(Icons.Rounded.Insights, c.dusk, tr("Сон", "Sleep"), avgSleep?.let { dur(it) } ?: "—", Modifier.weight(1f))
        Stat(Icons.Rounded.NightsStay, c.dusk, tr("Отбой", "Bedtime"), bed?.let { hm(it - 12 * 3600 + 86400) } ?: "—", Modifier.weight(1f))
        Stat(Icons.Rounded.WbTwilight, c.dawn, tr("Подъём", "Wake"), wake?.let(::hm) ?: "—", Modifier.weight(1f))
      }
    }
    if (nights.isNotEmpty()) item {
      WCard(Modifier.reveal(2)) {
        Text(tr("Длительность сна, ч", "Sleep duration, h"), fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = c.text)
        Row(Modifier.padding(top = 4.dp, bottom = 10.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
          Legend(c.ok, tr("хорошо", "good")); Legend(c.dusk, tr("средне", "okay")); Legend(c.danger, tr("плохо", "bad"))
        }
        SleepChart(nights)
      }
    }
    item { SectionTitle(tr("Последние ночи", "Recent nights")) }
    val rows = log.reversed().take(30)
    if (rows.isEmpty()) item {
      WCard { Muted(tr("Здесь появятся ночи: начните «Засыпание» вечером и выключите будильник утром.",
        "Nights will appear here: start “Fall asleep” in the evening and stop the alarm in the morning.")) }
    }
    itemsIndexed(rows) { i, n -> NightRow(n, Modifier.reveal(i + 3)) }
  }
}

@Composable
private fun Legend(color: Color, text: String) {
  Row(verticalAlignment = Alignment.CenterVertically) {
    Box(Modifier.size(10.dp).clip(RoundedCornerShape(3.dp)).background(color))
    Spacer(Modifier.width(5.dp))
    Muted(text, size = 12)
  }
}

@Composable
private fun Stat(icon: ImageVector, tint: Color, label: String, value: String, modifier: Modifier) {
  WCard(modifier, pad = 12) {
    IconBadge(icon, tint, 32)
    Spacer(Modifier.height(8.dp))
    Muted(label, size = 13)
    Text(value, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, style = Num, maxLines = 1, color = LocalWul.current.text)
  }
}

@Composable
private fun SleepChart(nights: List<SleepEntry>) {
  val c = LocalWul.current
  val grow = remember { Animatable(0f) }
  LaunchedEffect(nights.size) { grow.snapTo(0f); grow.animateTo(1f, tween(900)) }
  Row(Modifier.fillMaxWidth().height(150.dp), horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.Bottom) {
    val slots = maxOf(7, nights.size)
    nights.forEachIndexed { i, n ->
      val h = ((n.end - n.start) / 3600f).coerceIn(0f, 12f)
      val color = when { n.quality >= 3 -> c.ok; n.quality in 0..1 -> c.danger; else -> c.dusk }
      Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(String.format(java.util.Locale.US, "%.1f", h), fontSize = 10.sp, color = c.muted, style = Num)
        Canvas(Modifier.fillMaxWidth(.72f).height(112.dp)) {
          val k = ((grow.value * 1.5f) - i * .04f).coerceIn(0f, 1f)
          val bh = size.height * h / 12f * k
          drawRoundRect(Brush.verticalGradient(listOf(color, color.copy(alpha = .5f)), size.height - bh, size.height),
            Offset(0f, size.height - bh), Size(size.width, bh), CornerRadius(10f, 10f))
        }
        Text(dayOfMonth(n.end), fontSize = 11.sp, color = c.muted, style = Num)
      }
    }
    repeat(slots - nights.size) { Spacer(Modifier.weight(1f)) }
  }
}

/** День месяца из «локального как UTC» времени. */
private fun dayOfMonth(t: Long): String {
  val cal = java.util.Calendar.getInstance(java.util.TimeZone.getTimeZone("UTC")).apply { timeInMillis = t * 1000 }
  return pad(cal.get(java.util.Calendar.DAY_OF_MONTH))
}
private fun dayMonth(t: Long): String {
  val cal = java.util.Calendar.getInstance(java.util.TimeZone.getTimeZone("UTC")).apply { timeInMillis = t * 1000 }
  return pad(cal.get(java.util.Calendar.DAY_OF_MONTH)) + "." + pad(cal.get(java.util.Calendar.MONTH) + 1)
}

@Composable
private fun NightRow(n: SleepEntry, modifier: Modifier) {
  val c = LocalWul.current
  val ref = if (n.end > 0) n.end else n.start
  val q = quality()
  WCard(modifier) {
    Row {
      Column(Modifier.width(54.dp)) {
        Text(dayMonth(ref), style = Num, fontWeight = FontWeight.SemiBold, color = c.text)
        Muted(days()[((ref / 86400 + 3) % 7).toInt()], size = 13)
      }
      Column(Modifier.weight(1f)) {
        Row {
          Text("${if (n.start > 0) hm(n.start) else "—"} → ${if (n.end > 0) hm(n.end) else "—"}", style = Num,
            modifier = Modifier.weight(1f), color = c.text)
          if (n.start > 0 && n.end > 0) Muted(dur(n.end - n.start))
        }
        // шкала 18:00 → 12:00, полоса сна от заката к рассвету
        val pos = { t: Long -> (((t - 18 * 3600) % 86400 + 86400) % 86400 / (18f * 3600)).coerceIn(0f, 1f) }
        val l = if (n.start > 0) pos(n.start) else 0f
        val r = if (n.end > 0) pos(n.end) else 1f
        Canvas(Modifier.fillMaxWidth().padding(vertical = 8.dp).height(8.dp)) {
          drawRoundRect(c.surface2, cornerRadius = CornerRadius(8f, 8f))
          if (r > l) drawRoundRect(Brush.horizontalGradient(listOf(Color(0xFF7C3AED), Color(0xFFF59E0B)), size.width * l, size.width * r),
            Offset(size.width * l, 0f), Size(size.width * (r - l), size.height), CornerRadius(8f, 8f))
        }
        ChipRow {
          if (n.quality >= 0) Chip(null, tr("сон: ", "sleep: ") + q[n.quality])
          if (n.ease >= 0) Chip(null, tr("подъём: ", "wake: ") + q[n.ease])
          if (n.snoozes > 0) Chip(Icons.Rounded.Snooze, "${n.snoozes}")
        }
        if (n.note.isNotBlank()) Muted(n.note, Modifier.padding(top = 6.dp))
      }
    }
  }
}

@Composable
private fun MorningQuestion(vm: MainViewModel) {
  val c = LocalWul.current
  var q by remember { mutableIntStateOf(-1) }
  var e by remember { mutableIntStateOf(-1) }
  var note by remember { mutableStateOf("") }
  val pickBoth = tr("Выберите оба ответа", "Pick both answers")
  Box(Modifier.fillMaxWidth().reveal(0).clip(CardShape).background(c.surface)) {
    Box(Modifier.matchParentSize().background(Brush.verticalGradient(listOf(c.dawn.copy(alpha = .18f), Color.Transparent))))
    Column(Modifier.padding(16.dp)) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        IconBadge(Icons.Rounded.WbSunny, c.dawn, 40)
        Spacer(Modifier.width(12.dp))
        Column {
          Text(tr("Доброе утро! Как спалось?", "Good morning! How did you sleep?"), fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = c.text)
          Muted(tr("Ответ сохранится в статистике ночи.", "Saved to last night’s stats."), size = 13)
        }
      }
      QualityPills(tr("Качество сна", "Sleep quality"), q) { q = it }
      QualityPills(tr("Лёгкость подъёма", "Ease of waking"), e) { e = it }
      OutlinedTextField(note, { note = it }, label = { Text(tr("Заметка", "Note")) }, singleLine = true,
        leadingIcon = { Icon(Icons.Rounded.EditNote, null) },
        colors = fieldColors(), shape = CtlShape, modifier = Modifier.fillMaxWidth().padding(top = 10.dp))
      Spacer(Modifier.height(12.dp))
      GradientButton(tr("Сохранить ответ", "Save answer"), c.dawnBrush, c.onAccent) {
        if (q < 0 || e < 0) vm.say(pickBoth) else vm.morning(q, e, note) { q = -1; e = -1; note = "" }
      }
    }
  }
}

@Composable
fun fieldColors() = LocalWul.current.let { c ->
  OutlinedTextFieldDefaults.colors(
    focusedBorderColor = c.dawn, unfocusedBorderColor = c.border, focusedLabelColor = c.dawn, cursorColor = c.dawn,
    focusedTextColor = c.text, unfocusedTextColor = c.text, unfocusedLabelColor = c.muted,
    focusedLeadingIconColor = c.dawn, unfocusedLeadingIconColor = c.muted,
    focusedContainerColor = c.surface2, unfocusedContainerColor = c.surface2,
    focusedPlaceholderColor = c.muted, unfocusedPlaceholderColor = c.muted,
  )
}

/** Оценка 1–5 одной строкой; название выбранной — справа от заголовка. */
@Composable
private fun QualityPills(title: String, value: Int, onPick: (Int) -> Unit) {
  Row(Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 6.dp)) {
    Text(title, modifier = Modifier.weight(1f), color = LocalWul.current.text)
    AnimatedContent(if (value >= 0) quality()[value] else "—", transitionSpec = { fadeIn(tween(160)) togetherWith fadeOut(tween(120)) },
      label = "q") { Muted(it) }
  }
  Pills(listOf("1", "2", "3", "4", "5"), { it == value }) { onPick(it) }
}

// ---------------------------------------------------------------- Инфо
@Composable
fun InfoTab(s: WulState) {
  val c = LocalWul.current
  TabList {
    item { Title(tr("Как это работает", "How it works")) }
    item { InfoCard(0, Icons.Rounded.WbSunny, c.dawn, tr("Рассвет", "Sunrise"),
      tr("За 20–40 минут до будильника лампа начинает светиться тёмно-красным и постепенно разгорается до тёплого жёлтого света выбранной яркости — как восход солнца. В момент будильника включается звук и плавно становится громче.",
        "20–40 minutes before the alarm the lamp starts with a deep red glow and slowly brightens to warm yellow light — like a real sunrise. At alarm time the sound starts and gently gets louder."),
      tr("Длительность рассвета и яркость задаются для каждого будильника.", "Sunrise length and brightness are set per alarm.")) }
    item { InfoCard(1, Icons.Rounded.Snooze, c.dawn, tr("Повтор и выключение", "Snooze and stop"),
      tr("«Повтор» выключает звук на заданное время, свет остаётся. Кнопка Снуз на часах делает то же самое.",
        "Snooze silences the sound for a while, the light stays on. The Snooze button on the clock does the same.")) }
    item { InfoCard(2, Icons.Rounded.Bedtime, c.dusk, tr("Засыпание", "Fall asleep"),
      tr("Свет гаснет, как закат, за 5–60 минут; при желании — под тихую музыку, которая затихает вместе со светом.",
        "The light fades like a sunset over 5–60 minutes, optionally with quiet music that fades with it.")) }
    item { InfoCard(3, Icons.Rounded.Insights, c.ok, tr("Статистика", "Stats"),
      tr("Время отбоя (начало заката) и подъёма (выключение будильника) записываются автоматически. Утренний опрос помогает понять, какие настройки вам подходят.",
        "Bedtime (sunset start) and wake time (alarm stop) are logged automatically. The morning survey helps find the settings that suit you.")) }
    item { InfoCard(4, Icons.Rounded.Router, c.dawn, tr("Без iPhone", "No iPhone needed"),
      tr("Часы управляются модулем ESP32 вместо iPhone в доке: он выставляет время на табло, хранит будильники и запускает рассвет сам — даже без телефона и интернета.",
        "The clock is driven by an ESP32 module instead of an iPhone in the dock: it sets the display time, stores alarms and runs the sunrise on its own — even without a phone or internet."),
      tr("Модуль: ", "Module: ") + s.device.esp + if (s.device.fw.isNotBlank()) tr(" · часы ", " · clock ") + s.device.fw else "") }
  }
}

@Composable
private fun InfoCard(i: Int, icon: ImageVector, tint: Color, title: String, text: String, note: String? = null) {
  WCard(Modifier.reveal(i)) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      IconBadge(icon, tint, 40)
      Spacer(Modifier.width(12.dp))
      Text(title, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = LocalWul.current.text)
    }
    Spacer(Modifier.height(10.dp))
    Text(text, fontSize = 15.sp, lineHeight = 22.sp, color = LocalWul.current.text)
    if (note != null) Muted(note, Modifier.padding(top = 8.dp))
  }
}

// ---------------------------------------------------------------- Подключение
@SuppressLint("MissingPermission")
@Composable
fun ConnectScreen(vm: MainViewModel, link: Link, searching: Boolean, current: String?, bleFound: List<BleDevice>,
                  bleScanning: Boolean, onCancel: (() -> Unit)?, onDone: () -> Unit = {}) {
  val c = LocalWul.current
  val ctx = LocalContext.current
  var ip by remember { mutableStateOf(current ?: "") }
  var tab by remember { mutableStateOf(link) }
  val needPerm = tr("Нужно разрешение Bluetooth, чтобы найти модуль", "Bluetooth permission is needed to find the module")
  val perms = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { r ->
    if (r.values.all { it }) vm.scanBle() else vm.say(needPerm)
  }
  fun startBle() { if (hasBlePermissions(ctx)) vm.scanBle() else perms.launch(blePermissions()) }

  Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).imePadding().padding(24.dp)) {
    Spacer(Modifier.height(32.dp))
    Box(Modifier.size(84.dp).clip(RoundedCornerShape(26.dp)).background(c.dawnBrush), contentAlignment = Alignment.Center) {
      SunGlow(Modifier.size(84.dp))
    }
    Spacer(Modifier.height(20.dp))
    Text(tr("Подключение к часам", "Connect to the clock"), fontSize = 28.sp, fontWeight = FontWeight.Bold, color = c.text)
    Muted(tr("Как телефон будет связываться с модулем ESP32?", "How should the phone talk to the ESP32 module?"),
      Modifier.padding(top = 6.dp, bottom = 18.dp), 15)
    Pills(listOf("Wi-Fi", "Bluetooth"), { (it == 0) == (tab == Link.Wifi) }) { tab = if (it == 0) Link.Wifi else Link.Ble }
    Spacer(Modifier.height(18.dp))
    AnimatedContent(tab, transitionSpec = { fadeIn(tween(220)) togetherWith fadeOut(tween(160)) }, label = "conn") { t ->
      Column {
        if (t == Link.Wifi) {
          Muted(tr("Телефон и модуль — в одной Wi-Fi сети.", "Phone and module on the same Wi-Fi network."), Modifier.padding(bottom = 12.dp))
          GradientButton(if (searching) tr("Ищу модуль…", "Searching…") else tr("Найти автоматически", "Find automatically"),
            c.dawnBrush, c.onAccent, icon = Icons.Rounded.WifiFind, enabled = !searching) { vm.discover() }
          Spacer(Modifier.height(18.dp))
          OutlinedTextField(ip, { ip = it }, label = { Text(tr("IP-адрес модуля", "Module IP address")) },
            placeholder = { Text("192.168.1.61") }, leadingIcon = { Icon(Icons.Rounded.Wifi, null) },
            singleLine = true, colors = fieldColors(), shape = CtlShape, modifier = Modifier.fillMaxWidth())
          Spacer(Modifier.height(10.dp))
          GhostButton(tr("Подключиться", "Connect"), c.text) { vm.setHost(ip); onDone() }
          Spacer(Modifier.height(18.dp))
          Muted(tr("Часы сейчас играют как Bluetooth-колонка (Wi-Fi у них выключен)?", "Is the clock playing as a Bluetooth speaker right now (its Wi-Fi is off)?"),
            Modifier.padding(bottom = 8.dp), 13)
          SpeakerFindButton(vm)
        } else {
          Muted(tr("Модуль в режиме Bluetooth называется WakeUpLight-XXXX. Включите Bluetooth на телефоне.",
            "In Bluetooth mode the module is called WakeUpLight-XXXX. Turn on Bluetooth on the phone."), Modifier.padding(bottom = 12.dp))
          GradientButton(if (bleScanning) tr("Ищу…", "Scanning…") else tr("Найти по Bluetooth", "Scan for Bluetooth"),
            c.dawnBrush, c.onAccent, icon = Icons.AutoMirrored.Rounded.BluetoothSearching, enabled = !bleScanning) { startBle() }
          Spacer(Modifier.height(12.dp))
          if (bleScanning && bleFound.isEmpty()) Row(Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            CircularProgressIndicator(color = c.dawn, strokeWidth = 2.dp, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(10.dp)); Muted(tr("Поиск модулей рядом…", "Looking for modules nearby…"))
          }
          bleFound.forEachIndexed { i, d ->
            WCard(Modifier.padding(bottom = 8.dp).reveal(i), onClick = { vm.useBle(d); onDone() }, pad = 12) {
              Row(verticalAlignment = Alignment.CenterVertically) {
                IconBadge(Icons.Rounded.Bluetooth, c.dusk, 40)
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                  Text(d.name, color = c.text, fontWeight = FontWeight.SemiBold)
                  Muted("${d.address} · ${d.rssi} dBm", size = 12)
                }
                SignalBars(rssiLevel(d.rssi), c.dusk)
              }
            }
          }
        }
      }
    }
    if (onCancel != null) {
      Spacer(Modifier.height(10.dp))
      GhostButton(tr("Отмена", "Cancel"), c.muted, onClick = onCancel)
    }
  }
}

fun rssiLevel(rssi: Int) = when { rssi >= -55 -> 4; rssi >= -67 -> 3; rssi >= -78 -> 2; rssi > -95 -> 1; else -> 0 }

/** Четыре столбика уровня сигнала. */
@Composable
fun SignalBars(level: Int, color: Color, modifier: Modifier = Modifier) {
  val c = LocalWul.current
  Canvas(modifier.size(22.dp, 18.dp)) {
    val bw = size.width / 7
    for (i in 0..3) {
      val h = size.height * (i + 1) / 4
      drawRoundRect(if (i < level) color else c.border, Offset(bw * i * 2, size.height - h), Size(bw, h), CornerRadius(bw / 2, bw / 2))
    }
  }
}

// ---------------------------------------------------------------- Модуль не настроен
@Composable
fun SetupScreen(s: WulState, onChangeModule: () -> Unit) {
  val c = LocalWul.current
  val uri = LocalUriHandler.current
  Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp)) {
    Spacer(Modifier.height(32.dp))
    Box(Modifier.size(84.dp).clip(RoundedCornerShape(26.dp)).background(c.duskBrush), contentAlignment = Alignment.Center) {
      MoonGlow(Modifier.size(84.dp))
    }
    Spacer(Modifier.height(20.dp))
    Text(tr("Модуль не настроен", "Module is not set up"), fontSize = 28.sp, fontWeight = FontWeight.Bold, color = c.text)
    Muted(tr("Пройдите короткий мастер настройки — язык, регион, Wi-Fi и способ связи.",
      "Run the short setup wizard — language, region, Wi-Fi and how to connect."), Modifier.padding(top = 6.dp, bottom = 18.dp), 15)
    val ap = s.net.ble.ifBlank { "WakeUpLight-XXXX" }
    listOf(
      Icons.Rounded.Wifi to tr("Подключите телефон к Wi-Fi «$ap»", "Connect the phone to Wi-Fi “$ap”"),
      Icons.Rounded.Key to tr("Пароль: wakeuplight", "Password: wakeuplight"),
      Icons.Rounded.OpenInBrowser to tr("Мастер откроется сам или по адресу 192.168.4.1", "The wizard opens by itself or at 192.168.4.1"),
    ).forEachIndexed { i, (icon, text) ->
      WCard(Modifier.padding(bottom = 8.dp).reveal(i), pad = 14) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Box(Modifier.size(28.dp).clip(RoundedCornerShape(50)).background(c.dawn), contentAlignment = Alignment.Center) {
            Text("${i + 1}", color = c.onAccent, fontWeight = FontWeight.Bold, fontSize = 14.sp)
          }
          Spacer(Modifier.width(12.dp))
          Icon(icon, null, tint = c.muted, modifier = Modifier.size(20.dp))
          Spacer(Modifier.width(8.dp))
          Text(text, color = c.text, fontSize = 15.sp)
        }
      }
    }
    Spacer(Modifier.height(14.dp))
    GradientButton(tr("Открыть мастер", "Open the wizard"), c.dawnBrush, c.onAccent, icon = Icons.Rounded.OpenInBrowser) {
      runCatching { uri.openUri("http://192.168.4.1/") }
    }
    Spacer(Modifier.height(10.dp))
    GhostButton(tr("Другой модуль", "Another module"), c.text, onClick = onChangeModule)
  }
}
