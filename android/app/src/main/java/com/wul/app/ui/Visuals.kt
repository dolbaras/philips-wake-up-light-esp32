package com.wul.app.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin

/** Солнце с медленно вращающимися лучами и «дыханием» свечения. */
@Composable
fun SunGlow(modifier: Modifier, core: Color = Color(0xFFFFF7D6), ray: Color = Color.White.copy(alpha = .55f)) {
  val t = rememberInfiniteTransition(label = "sun")
  val spin by t.animateFloat(0f, 360f, infiniteRepeatable(tween(60_000, easing = LinearEasing)), label = "spin")
  val breathe by t.animateFloat(.92f, 1.08f, infiniteRepeatable(tween(3200), RepeatMode.Reverse), label = "breathe")
  Canvas(modifier) {
    val r = size.minDimension / 2
    val center = Offset(size.width / 2, size.height / 2)
    drawCircle(Brush.radialGradient(listOf(core.copy(alpha = .55f), Color.Transparent), center, r * breathe), r * breathe, center)
    rotate(spin, center) {
      repeat(12) { i ->
        val a = i * (2 * PI / 12).toFloat()
        val inner = r * .46f
        val outer = r * (if (i % 2 == 0) .80f else .68f) * breathe
        drawLine(ray, Offset(center.x + cos(a) * inner, center.y + sin(a) * inner),
          Offset(center.x + cos(a) * outer, center.y + sin(a) * outer), strokeWidth = r * .06f, cap = StrokeCap.Round)
      }
    }
    drawCircle(Brush.radialGradient(listOf(Color.White, core), center, r * .36f), r * .34f, center)
  }
}

/** Луна (серп) с мерцающими звёздами. */
@Composable
fun MoonGlow(modifier: Modifier, moon: Color = Color(0xFFEDE9FE)) {
  val t = rememberInfiniteTransition(label = "moon")
  val tw = (0 until 6).map { i -> t.animateFloat(.2f, 1f, infiniteRepeatable(tween(1400 + i * 260), RepeatMode.Reverse), label = "s$i") }
  val stars = remember { listOf(.12f to .22f, .82f to .18f, .9f to .62f, .2f to .78f, .62f to .1f, .5f to .88f) }
  Canvas(modifier) {
    val r = size.minDimension * .26f
    val c = Offset(size.width / 2, size.height / 2)
    drawCircle(Brush.radialGradient(listOf(moon.copy(alpha = .35f), Color.Transparent), c, r * 2.2f), r * 2.2f, c)
    val crescent = Path().apply {
      addOval(androidx.compose.ui.geometry.Rect(c, r))
    }
    val cut = Path().apply { addOval(androidx.compose.ui.geometry.Rect(Offset(c.x + r * .45f, c.y - r * .35f), r * .92f)) }
    val p = Path().apply { op(crescent, cut, androidx.compose.ui.graphics.PathOperation.Difference) }
    drawPath(p, moon)
    stars.forEachIndexed { i, (x, y) ->
      val a = tw[i].value
      val s = Offset(size.width * x, size.height * y)
      val k = size.minDimension * .025f
      drawLine(moon.copy(alpha = a), Offset(s.x - k, s.y), Offset(s.x + k, s.y), strokeWidth = k * .5f, cap = StrokeCap.Round)
      drawLine(moon.copy(alpha = a), Offset(s.x, s.y - k), Offset(s.x, s.y + k), strokeWidth = k * .5f, cap = StrokeCap.Round)
    }
  }
}

/** Цвет рассвета на доле пути 0..1: тёмно-красный → оранжевый → тёплый жёлтый. */
fun dawnColor(f: Float): Color {
  val stops = listOf(Color(0xFF7F1D1D), Color(0xFFDC2626), Color(0xFFF97316), Color(0xFFFBBF24), Color(0xFFFEF3C7))
  val x = f.coerceIn(0f, 1f) * (stops.size - 1)
  val i = x.toInt().coerceAtMost(stops.size - 2)
  return androidx.compose.ui.graphics.lerp(stops[i], stops[i + 1], x - i)
}

/**
 * Кривая рассвета: как лампа разгорается за [minutes] до будильника до яркости [intensity] (1..20).
 * Заливка под кривой — цвет света в каждый момент.
 */
@Composable
fun SunriseCurve(minutes: Int, intensity: Int, alarmTime: String, modifier: Modifier = Modifier) {
  val c = LocalWul.current
  val h by animateFloatAsState(intensity / 20f, spring(), label = "h")
  val len by animateFloatAsState(minutes / 40f, spring(), label = "len")
  Column(modifier.fillMaxWidth()) {
    Canvas(Modifier.fillMaxWidth().height(110.dp)) {
      val w = size.width; val hh = size.height
      val start = w * (1f - len)                    // начало рассвета (чем длиннее, тем левее)
      val top = hh * (1f - .9f * h)
      val pts = (0..40).map { i ->
        val f = i / 40f
        val x = start + (w - start) * f
        val y = hh - (hh - top) * f.pow(1.8f)       // свет нарастает медленно, потом быстрее
        Offset(x, y)
      }
      // сетка
      for (k in 1..3) drawLine(c.border, Offset(0f, hh * k / 4), Offset(w, hh * k / 4), 1f)
      val fill = Path().apply {
        moveTo(start, hh); pts.forEach { lineTo(it.x, it.y) }; lineTo(w, hh); close()
      }
      drawPath(fill, Brush.horizontalGradient((0..6).map { dawnColor(it / 6f * h).copy(alpha = .85f) }, start, w))
      val line = Path().apply { moveTo(pts[0].x, pts[0].y); pts.drop(1).forEach { lineTo(it.x, it.y) } }
      drawPath(line, Color.White.copy(alpha = if (c.dark) .8f else .95f), style = Stroke(3.dp.toPx(), cap = StrokeCap.Round))
      drawCircle(dawnColor(h), 6.dp.toPx(), pts.last())
      drawCircle(Color.White, 3.dp.toPx(), pts.last())
    }
    Row(Modifier.fillMaxWidth().padding(top = 4.dp)) {
      Text("−${minutes} " + tr("мин", "min"), fontSize = 12.sp, color = c.muted, style = Num, modifier = Modifier.weight(1f))
      Text(alarmTime, fontSize = 12.sp, color = c.dawn, style = Num)
    }
  }
}

/** Полупрозрачная волна света внизу больших карточек. */
@Composable
fun Waves(color: Color, modifier: Modifier) {
  val t = rememberInfiniteTransition(label = "waves")
  val ph by t.animateFloat(0f, (2 * PI).toFloat(), infiniteRepeatable(tween(9000, easing = LinearEasing)), label = "ph")
  Canvas(modifier) {
    for (layer in 0..1) {
      val p = Path()
      val amp = size.height * (.18f + layer * .08f)
      val base = size.height * (.55f + layer * .15f)
      p.moveTo(0f, size.height)
      var x = 0f
      while (x <= size.width) {
        p.lineTo(x, base + amp * sin(x / size.width * 2 * PI.toFloat() * (1 + layer) + ph * (if (layer == 0) 1 else -1)))
        x += 6f
      }
      p.lineTo(size.width, size.height); p.close()
      drawPath(p, color.copy(alpha = .10f + layer * .06f))
    }
  }
}
