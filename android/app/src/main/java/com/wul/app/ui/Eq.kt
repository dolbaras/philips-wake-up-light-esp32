package com.wul.app.ui

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.wul.app.MainViewModel
import com.wul.app.data.WulState
import kotlin.math.abs

/** Пресеты эквалайзера модуля — id как в прошивке (core/eq.h); «свой» — модуль ставит сам, когда двигают ручки. */
@Composable
fun eqPresetNames(): List<Pair<String, String>> = listOf(
  "off" to tr("Выкл", "Off"), "soft" to tr("Мягкий", "Gentle"), "warm" to tr("Тёплый", "Warm"),
  "clear" to tr("Чёткий", "Clear"), "loud" to tr("Громкий", "Loud"), "custom" to tr("Свой", "Custom"),
)

/** Мелодия для «Послушать» — «Йога»: музыка, на ней слышно и бас, и верха. */
const val EQ_LISTEN_IDX = 6

/**
 * Звучание динамика часов: пресеты и четыре ручки. [listen] — кнопка «Послушать» (мелодия из часов; только в Wi-Fi —
 * в режиме колонки слушают музыку телефона). Работает по любому каналу: Wi-Fi или Bluetooth колонки.
 */
@Composable
fun EqCard(s: WulState, vm: MainViewModel, listen: Boolean, modifier: Modifier = Modifier) {
  val c = LocalWul.current
  val eq = s.eq
  val names = eqPresetNames()
  val rows = listOf(names.take(3), names.drop(3))
  val db = tr("дБ", "dB"); val hzS = tr("Гц", "Hz"); val offS = tr("выкл", "off")
  val fmtDb: (Int) -> String = { v -> (if (v > 0) "+" else if (v < 0) "−" else "") + "${abs(v)} $db" }
  val fmtHz: (Int) -> String = { v -> if (v <= 50) offS else "$v $hzS" }
  var playing by remember { mutableStateOf(false) }
  val stop by rememberUpdatedState { if (playing) vm.previewStop() }
  DisposableEffect(Unit) { onDispose { stop() } }   // ушли с экрана — мелодию не оставлять играть
  WCard(modifier, pad = 12) {
    rows.forEachIndexed { r, row ->
      if (r > 0) Spacer(Modifier.height(6.dp))
      Pills(row.map { it.second }, { i -> eq.preset == row[i].first }, c.dusk) { i ->
        val id = row[i].first
        if (id != "custom" && id != eq.preset) vm.eqPreset(id)
      }
    }
    SliderRow(tr("Срез баса", "Bass cut"), eq.hp, 50..250, fmtHz, c.dusk, step = 10) { vm.eqSet(hp = it) }
    SliderRow(tr("Плотность · 280 Гц", "Body · 280 Hz"), eq.body, -6..6, fmtDb, c.dusk) { vm.eqSet(body = it) }
    SliderRow(tr("Чёткость · 3 кГц", "Clarity · 3 kHz"), eq.presence, -6..6, fmtDb, c.dusk) { vm.eqSet(presence = it) }
    SliderRow(tr("Верха · от 8 кГц", "Treble · 8 kHz+"), eq.treble, -6..6, fmtDb, c.dusk) { vm.eqSet(treble = it) }
    Spacer(Modifier.height(6.dp))
    Muted(if (eq.on) tr("Действует на всё: музыку с телефона, будильники и закат. Срез баса бережёт динамик без короба от хрипа.",
      "Applies to everything: phone music, alarms and dusk. The bass cut keeps the enclosure-less speaker from rattling.")
    else tr("Обработка выключена — звук как есть. Любая ручка или пресет её включат.",
      "Processing is off — sound as is. Any knob or preset turns it on."), size = 12)
    if (listen) {
      Spacer(Modifier.height(10.dp))
      GhostButton(if (playing) tr("Остановить", "Stop") else tr("Послушать — «Йога»", "Listen — “Yoga”"), c.dusk, Modifier.fillMaxWidth(),
        icon = if (playing) Icons.Rounded.Stop else Icons.Rounded.PlayArrow) {
        if (playing) vm.previewStop() else vm.preview(EQ_LISTEN_IDX, s.volume.takeIf { it > 0 } ?: 9)
        playing = !playing
      }
    }
  }
}
