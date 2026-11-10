package com.wul.app.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Equalizer
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.TextFields
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.wul.app.MainViewModel
import com.wul.app.data.WulState

// ---------------------------------------------------------------- семисегментник

/** Допустимые знаки табло (API.md, «Табло»): цифры и похожие на них буквы. */
const val DISPLAY_CHARS = "0123456789OIZSbGBg "

/** Какой цифрой знак покажут часы. */
private fun asDigit(ch: Char): Char? = when (ch) {
  in '0'..'9' -> ch; 'O' -> '0'; 'I' -> '1'; 'Z' -> '2'; 'S' -> '5'; 'b', 'G' -> '6'; 'B' -> '8'; 'g' -> '9'; else -> null
}

/** Сегменты a b c d e f g (биты 0..6) для цифры. */
private fun segments(d: Char?): Int = when (d) {
  '0' -> 0b0111111; '1' -> 0b0000110; '2' -> 0b1011011; '3' -> 0b1001111; '4' -> 0b1100110
  '5' -> 0b1101101; '6' -> 0b1111101; '7' -> 0b0000111; '8' -> 0b1111111; '9' -> 0b1101111
  else -> 0
}

/** Нормализация ввода: строчные o/i/z/s → заглавные, недопустимые знаки выкидываются, не длиннее 4. */
fun cleanDisplayText(raw: String): String =
  raw.map { when (it) { 'o' -> 'O'; 'i' -> 'I'; 'z' -> 'Z'; 's' -> 'S'; else -> it } }
    .filter { it in DISPLAY_CHARS }.joinToString("").take(4)

/** Пустая половинка табло — только пробел парой: «  12» можно, « 1 2» нельзя. */
fun displayTextValid(t: String) = t.length == 4 && (t[0] == ' ') == (t[1] == ' ') && (t[2] == ' ') == (t[3] == ' ')

/** Табло часов: 4 знака под семисегментник, двоеточие посередине; погасшие сегменты — едва видны. */
@Composable
fun SevenSegment(text: String, modifier: Modifier = Modifier, lit: Color = Color(0xFFFFB347), colon: Boolean = true) {
  val chars = text.padEnd(4).take(4)
  Canvas(modifier.fillMaxWidth().aspectRatio(3.1f)) {
    val digitW = size.width / 4.6f
    val gap = digitW * .18f
    val colonW = digitW * .42f
    val totalW = digitW * 4 + gap * 2 + colonW
    var x = (size.width - totalW) / 2
    val h = size.height * .86f
    val top = (size.height - h) / 2
    val off = lit.copy(alpha = .07f)
    fun seg(path: Path, on: Boolean) = drawPath(path, if (on) lit else off)
    fun digit(x0: Float, mask: Int) {
      val t = digitW * .16f                  // толщина сегмента
      val sk = digitW * .10f                 // наклон, как у настоящих индикаторов
      val w = digitW * .82f
      val half = h / 2
      fun px(xx: Float, yy: Float) = Offset(x0 + xx + sk * (1f - yy / h), top + yy)
      fun horiz(yc: Float) = Path().apply {
        val a = px(t * .6f, yc); val b = px(w - t * .6f, yc)
        moveTo(a.x, a.y); lineTo(a.x + t / 2, a.y - t / 2); lineTo(b.x - t / 2, b.y - t / 2); lineTo(b.x, b.y)
        lineTo(b.x - t / 2, b.y + t / 2); lineTo(a.x + t / 2, a.y + t / 2); close()
      }
      fun vert(xc: Float, y0: Float, y1: Float) = Path().apply {
        val a = px(xc, y0 + t * .6f); val b = px(xc, y1 - t * .6f)
        moveTo(a.x, a.y); lineTo(a.x + t / 2, a.y + t / 2); lineTo(b.x + t / 2, b.y - t / 2); lineTo(b.x, b.y)
        lineTo(b.x - t / 2, b.y - t / 2); lineTo(a.x - t / 2, a.y + t / 2); close()
      }
      seg(horiz(t / 2), mask and 1 != 0)                         // a
      seg(vert(w - t / 2, t / 2, half), mask and 2 != 0)          // b
      seg(vert(w - t / 2, half, h - t / 2), mask and 4 != 0)      // c
      seg(horiz(h - t / 2), mask and 8 != 0)                      // d
      seg(vert(t / 2, half, h - t / 2), mask and 16 != 0)         // e
      seg(vert(t / 2, t / 2, half), mask and 32 != 0)             // f
      seg(horiz(half), mask and 64 != 0)                          // g
    }
    for (i in 0..3) {
      digit(x, segments(asDigit(chars[i])))
      x += digitW + if (i == 1) 0f else gap
      if (i == 1) {                                               // двоеточие
        val r = colonW * .16f
        val cx = x + colonW / 2
        drawCircle(if (colon) lit else off, r, Offset(cx, top + h * .33f))
        drawCircle(if (colon) lit else off, r, Offset(cx, top + h * .67f))
        x += colonW
      }
    }
  }
}

/** Эквалайзер на табло (демо): полосы 0..5 → цифры 1, 7, 4, 3, 9, 8. */
@Composable
private fun EqPreview(modifier: Modifier) {
  val t = rememberInfiniteTransition(label = "eqp")
  val lv = (0..3).map { i -> t.animateFloat(0f, 5.99f, infiniteRepeatable(tween(500 + i * 130), RepeatMode.Reverse), label = "e$i") }
  val map = "174398"
  SevenSegment(lv.joinToString("") { map[it.value.toInt()].toString() }, modifier, colon = false)
}

// ---------------------------------------------------------------- страница «Табло»

@Composable
fun DisplayPage(s: WulState, vm: MainViewModel, now: Long, onBack: () -> Unit) {
  val c = LocalWul.current
  var text by rememberSaveable { mutableStateOf("BOSS") }
  var durIdx by rememberSaveable { mutableIntStateOf(1) }
  val durs = listOf(10, 60, 600, 0)
  val valid = displayTextValid(text)
  val d = s.display
  val left = if (d.secondsLeft > 0) maxOf(0, d.secondsLeft - (now - s.time).toInt()) else 0
  TabList {
    item { SubTitle(tr("Табло", "Display"), onBack) }
    item {
      // табло «как на часах»
      Column(Modifier.fillMaxWidth().reveal(0).clip(CardShape).background(Color(0xFF07090F)).padding(18.dp)) {
        AnimatedContent(d.mode == "eq", transitionSpec = { fadeIn(tween(250)) togetherWith fadeOut(tween(200)) }, label = "prev") { eq ->
          if (eq) EqPreview(Modifier) else SevenSegment(text)
        }
        Spacer(Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
          Icon(when (d.mode) { "text" -> Icons.Rounded.TextFields; "eq" -> Icons.Rounded.Equalizer; else -> Icons.Rounded.Schedule },
            null, tint = Color(0xFFFFB347), modifier = Modifier.padding(end = 6.dp).size(18.dp))
          Text(
            tr("сейчас: ", "now: ") + when (d.mode) {
              "text" -> "«${d.text}»" + if (left > 0) " · " + secondsText(left) else tr(" · пока не вернут", " · until reset")
              "eq" -> tr("эквалайзер", "equalizer") + if (left > 0) " · " + secondsText(left) else ""
              else -> tr("часы", "clock")
            },
            color = Color(0xFFFDE68A), fontSize = 14.sp,
          )
        }
      }
    }
    item {
      WCard(Modifier.reveal(1)) {
        Text(tr("Надпись", "Text"), color = c.text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
          text, { text = cleanDisplayText(it) }, singleLine = true,
          textStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 34.sp, letterSpacing = 14.sp,
            textAlign = TextAlign.Center, fontWeight = FontWeight.Bold, color = c.text),
          keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
          isError = text.isNotEmpty() && !valid, colors = fieldColors(), shape = CtlShape, modifier = Modifier.fillMaxWidth(),
        )
        Muted(
          if (text.isNotEmpty() && !valid) {
            if (text.length < 4) tr("Нужно ровно 4 знака.", "Exactly 4 characters are needed.")
            else tr("Пробел гасит половинку табло — только парой: «\u00A0\u00A012» можно, «\u00A01\u00A02» нельзя.",
              "A space blanks half of the display — only in pairs: “\u00A0\u00A012” is fine, “\u00A01\u00A02” is not.")
          } else tr("Знаки: 0–9 и O I Z S b G B g. Пробел — только парой (гасит половинку).",
            "Characters: 0–9 and O I Z S b G B g. Spaces only in pairs (blank half).") ,
          Modifier.padding(top = 6.dp), 13,
        )
        Spacer(Modifier.height(10.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
          listOf("BOSS", "1234", "  12", "0000").forEach { p ->
            Text("«" + p.replace(' ', '\u00A0') + "»", fontSize = 13.sp, color = if (p == text) c.dawn else c.muted, fontFamily = FontFamily.Monospace,
              modifier = Modifier.clip(RoundedCornerShape(50)).background(if (p == text) c.dawn.copy(alpha = .14f) else c.surface2)
                .bouncy { text = p }.padding(horizontal = 10.dp, vertical = 8.dp))
          }
        }
        Spacer(Modifier.height(14.dp))
        Text(tr("Сколько показывать", "Show for"), color = c.text, fontSize = 15.sp)
        Spacer(Modifier.height(8.dp))
        Pills(listOf(tr("10 с", "10 s"), tr("1 мин", "1 min"), tr("10 мин", "10 min"), tr("Всегда", "Always")), { it == durIdx }) { durIdx = it }
        Spacer(Modifier.height(14.dp))
        GradientButton(tr("Показать", "Show"), c.dawnBrush, c.onAccent, icon = Icons.Rounded.TextFields, enabled = valid) {
          vm.displayText(text, durs[durIdx])
        }
        Spacer(Modifier.height(8.dp))
        GhostButton(tr("Вернуть часы", "Back to clock"), c.text, icon = Icons.Rounded.Schedule) { vm.displayClock() }
      }
    }
    item {
      WCard(Modifier.reveal(2)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          IconBadge(Icons.Rounded.Equalizer, c.dusk, 36); Spacer(Modifier.width(12.dp))
          Column(Modifier.weight(1f)) {
            Text(tr("Эквалайзер", "Equalizer"), color = c.text, fontSize = 16.sp)
            Muted(tr("демо на табло, 10 секунд (пока без звука)", "demo on the display, 10 seconds (no sound yet)"), size = 13)
          }
        }
        Spacer(Modifier.height(10.dp))
        GhostButton(tr("Эквалайзер — демо 10 с", "Equalizer — 10 s demo"), c.dusk, icon = Icons.Rounded.Equalizer) { vm.displayEq(10) }
      }
    }
  }
}

@Composable
private fun secondsText(n: Int) = if (n >= 60) tr("${n / 60} мин ${n % 60} с", "${n / 60} min ${n % 60} s") else tr("$n с", "$n s")
