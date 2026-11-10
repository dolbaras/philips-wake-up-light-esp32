package com.wul.app.ui

import android.app.Activity
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AlarmOff
import androidx.compose.material.icons.rounded.DarkMode
import androidx.compose.material.icons.rounded.Snooze
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import com.wul.app.MainViewModel
import com.wul.app.data.Mode
import com.wul.app.data.WulState

/** Полноэкранные состояния: утро (рассвет/звонок/повтор) и ночь (закат). */
@Composable
fun FullScreenState(s: WulState, now: Long, vm: MainViewModel) {
  when (s.mode) {
    Mode.Sunrise, Mode.Ringing, Mode.Snoozed -> Morning(s, now, vm)
    Mode.Dusk -> Night(s, now, vm)
    Mode.Idle -> {}
  }
}

@Composable
private fun Blocker(brush: Brush, content: @Composable BoxScope.() -> Unit) {
  Box(
    Modifier.fillMaxSize().background(brush)
      .clickable(remember { MutableInteractionSource() }, indication = null) {},   // не пропускать нажатия к вкладкам
    contentAlignment = Alignment.Center,
  ) { content() }
}

@Composable
private fun Morning(s: WulState, now: Long, vm: MainViewModel) {
  val a = s.alarms.firstOrNull { it.id == s.activeAlarm }
  val ink = Color(0xFF1A1205)
  // фон светлый — тёмные значки строки состояния, пока открыт экран
  val view = LocalView.current
  DisposableEffect(Unit) {
    val ctl = (view.context as? Activity)?.window?.let { WindowCompat.getInsetsController(it, view) }
    val was = ctl?.isAppearanceLightStatusBars
    ctl?.isAppearanceLightStatusBars = true
    onDispose { if (was != null) ctl.isAppearanceLightStatusBars = was }
  }
  // небо «светлеет» — верх градиента медленно дышит
  val t = rememberInfiniteTransition(label = "sky")
  val k by t.animateFloat(0f, 1f, infiniteRepeatable(tween(6000), RepeatMode.Reverse), label = "k")
  val sky = Brush.verticalGradient(listOf(
    androidx.compose.ui.graphics.lerp(Color(0xFFFDE68A), Color(0xFFFEF3C7), k), Color(0xFFF59E0B), Color(0xFFF97362)))
  Blocker(sky) {
    Waves(Color.White, Modifier.fillMaxWidth().height(240.dp).align(Alignment.BottomCenter))
    Column(Modifier.fillMaxSize().systemBarsPadding().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
      Spacer(Modifier.weight(1f))
      Box(contentAlignment = Alignment.Center) {
        SunGlow(Modifier.size(300.dp), core = Color(0xFFFFFBEB), ray = Color.White.copy(alpha = .7f))
        FlipText(hm(now), TextStyle(fontSize = 64.sp, fontWeight = FontWeight.Bold, fontFeatureSettings = "tnum"), ink)
      }
      Text(if (s.mode == Mode.Sunrise) tr("Рассвет", "Sunrise") else tr("Доброе утро", "Good morning"),
        fontSize = 32.sp, fontWeight = FontWeight.SemiBold, color = ink)
      Text(
        when (s.mode) {
          Mode.Sunrise -> s.next?.let { tr("будильник в ", "alarm at ") + hm(it.time) } ?: tr("свет разгорается", "the light is rising")
          Mode.Snoozed -> tr("повтор в ", "snoozed until ") + hm(s.snoozeUntil)
          else -> a?.label?.ifBlank { null } ?: tr("пора вставать", "time to get up")
        },
        fontSize = 17.sp, color = ink.copy(alpha = .75f), modifier = Modifier.padding(top = 4.dp),
      )
      Spacer(Modifier.weight(1f))
      Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (s.mode == Mode.Ringing) GradientButton(tr("Повтор через ", "Snooze for ") + minutes(a?.snoozeMin ?: 9),
          Brush.linearGradient(listOf(ink, Color(0xFF3B2A12))), Color(0xFFFDE68A), icon = Icons.Rounded.Snooze) { vm.snooze() }
        GhostButton(tr("Выключить", "Stop"), ink, icon = Icons.Rounded.AlarmOff) { vm.stopAlarm() }
      }
    }
  }
}

@Composable
private fun Night(s: WulState, now: Long, vm: MainViewModel) {
  val left = maxOf(0, s.duskEnd - now)
  Blocker(Brush.verticalGradient(listOf(Color(0xFF312E81), Color(0xFF1E1B4B), Color(0xFF0B1120)))) {
    Column(Modifier.fillMaxSize().systemBarsPadding().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
      Spacer(Modifier.weight(1f))
      MoonGlow(Modifier.size(260.dp))
      FlipText(hm(now), TextStyle(fontSize = 56.sp, fontWeight = FontWeight.Bold, fontFeatureSettings = "tnum"), Color(0xFFEDE9FE))
      Text(tr("Спокойной ночи", "Good night"), fontSize = 28.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFFEDE9FE))
      Text(tr("свет погаснет через ", "light off in ") + minutes(((left + 59) / 60).toInt()), fontSize = 16.sp,
        color = Color(0xFFC4B5FD), modifier = Modifier.padding(top = 4.dp))
      Spacer(Modifier.weight(1f))
      Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        GhostButton(tr("Остановить", "Stop"), Color(0xFFEDE9FE), Modifier.weight(1f), icon = Icons.Rounded.Stop) { vm.duskStop() }
        GradientButton(tr("Погасить", "Lights off"), Brush.linearGradient(listOf(Color(0xFFEDE9FE), Color(0xFFC4B5FD))), Color(0xFF1E1B4B),
          Modifier.weight(1f), icon = Icons.Rounded.DarkMode) { vm.duskStop(off = true) }
      }
      Text(tr("«Остановить» — закат прервётся, свет останется как сейчас. «Погасить» — лампа погаснет за 3–5 с.",
        "“Stop” ends the sunset and keeps the light as it is. “Lights off” turns the lamp off in 3–5 s."),
        fontSize = 12.sp, color = Color(0xFFC4B5FD).copy(alpha = .8f), textAlign = TextAlign.Center, lineHeight = 17.sp,
        modifier = Modifier.padding(top = 10.dp, bottom = 4.dp))
    }
  }
}
