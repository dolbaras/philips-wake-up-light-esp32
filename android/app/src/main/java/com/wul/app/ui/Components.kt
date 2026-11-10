package com.wul.app.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt

val CardShape = RoundedCornerShape(22.dp)
val CtlShape = RoundedCornerShape(14.dp)

/** Цифры одинаковой ширины — время не «прыгает». */
val Num = TextStyle(fontFeatureSettings = "tnum")

fun pad(n: Int) = n.toString().padStart(2, '0')
fun hm(t: Long) = pad(((t / 3600) % 24).toInt()) + ":" + pad(((t / 60) % 60).toInt())

/** Цвет текста/значков поверх акцентного фона. */
val WulColors.onAccent get() = if (dark) onDawn else Color.White

// ---------------------------------------------------------------- движение

/** Пружинное «вжатие» при нажатии + обычный клик. */
fun Modifier.bouncy(role: Role = Role.Button, enabled: Boolean = true, onClick: () -> Unit): Modifier = composed {
  val src = remember { MutableInteractionSource() }
  val pressed by src.collectIsPressedAsState()
  val k by animateFloatAsState(if (pressed) .96f else 1f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessMedium), label = "press")
  graphicsLayer { scaleX = k; scaleY = k }
    .clickable(interactionSource = src, indication = androidx.compose.material3.ripple(), enabled = enabled, role = role, onClick = onClick)
}

/** Мягкое появление (снизу + прозрачность) с задержкой по порядку элемента. */
fun Modifier.reveal(index: Int = 0): Modifier = composed {
  val p = remember { Animatable(0f) }
  LaunchedEffect(Unit) { p.animateTo(1f, tween(380, delayMillis = (index * 45).coerceAtMost(300))) }
  graphicsLayer { alpha = p.value; translationY = (1f - p.value) * 36f }
}

// ---------------------------------------------------------------- карточки и подписи

@Composable
fun WCard(modifier: Modifier = Modifier, onClick: (() -> Unit)? = null, pad: Int = 16, content: @Composable ColumnScope.() -> Unit) {
  val c = LocalWul.current
  Column(
    modifier
      .fillMaxWidth()
      .then(if (onClick != null) Modifier.bouncy(onClick = onClick) else Modifier)
      .shadow(if (c.dark) 0.dp else 6.dp, CardShape, ambientColor = Color(0x22B45309), spotColor = Color(0x22B45309))
      .clip(CardShape)
      .background(c.surface)
      .border(1.dp, c.border, CardShape)
      .padding(pad.dp),
    content = content,
  )
}

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
  Text(text.uppercase(), color = LocalWul.current.muted, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
    letterSpacing = 0.8.sp, modifier = modifier.padding(start = 6.dp, top = 10.dp, bottom = 2.dp))
}

@Composable
fun Muted(text: String, modifier: Modifier = Modifier, size: Int = 14) =
  Text(text, color = LocalWul.current.muted, fontSize = size.sp, lineHeight = (size + 6).sp, modifier = modifier)

/** Значок в цветной «таблетке». */
@Composable
fun IconBadge(icon: ImageVector, tint: Color, size: Int = 40, modifier: Modifier = Modifier) {
  Box(
    modifier.size(size.dp).clip(RoundedCornerShape((size * .32f).dp)).background(tint.copy(alpha = .16f)),
    contentAlignment = Alignment.Center,
  ) { Icon(icon, null, tint = tint, modifier = Modifier.size((size * .55f).dp)) }
}

@Composable
fun SwitchRow(
  title: String, checked: Boolean, sub: String? = null, accent: Color = LocalWul.current.dawn,
  icon: ImageVector? = null, onChange: (Boolean) -> Unit,
) {
  val c = LocalWul.current
  Row(
    Modifier.fillMaxWidth().heightIn(min = 56.dp).clip(CtlShape).clickable(role = Role.Switch) { onChange(!checked) }.padding(vertical = 4.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    if (icon != null) { IconBadge(icon, accent, 36); Spacer(Modifier.width(12.dp)) }
    Column(Modifier.weight(1f)) {
      Text(title, fontSize = 16.sp, color = c.text)
      if (sub != null) Muted(sub, size = 13)
    }
    Spacer(Modifier.width(8.dp))
    Switch(checked, onCheckedChange = null, colors = SwitchDefaults.colors(
      checkedThumbColor = c.onAccent, checkedTrackColor = accent, checkedBorderColor = accent,
      uncheckedTrackColor = c.surface2, uncheckedBorderColor = c.border, uncheckedThumbColor = c.muted))
  }
}

/** Строка-переход в подраздел. */
@Composable
fun NavRow(icon: ImageVector, tint: Color, title: String, sub: String?, onClick: () -> Unit) {
  val c = LocalWul.current
  Row(
    Modifier.fillMaxWidth().heightIn(min = 60.dp).clip(CtlShape).bouncy(onClick = onClick).padding(vertical = 6.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    IconBadge(icon, tint, 40)
    Spacer(Modifier.width(14.dp))
    Column(Modifier.weight(1f)) {
      Text(title, fontSize = 16.sp, color = c.text)
      if (sub != null) Muted(sub, size = 13)
    }
    Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null, tint = c.muted)
  }
}

// ---------------------------------------------------------------- слайдер

/**
 * Свой ползунок: толстый трек с градиентом, крупный бегунок, «пузырь» значения при перетаскивании,
 * значки по краям, лёгкая вибрация на каждом шаге. [onCommit] — когда палец отпущен.
 */
@Composable
fun GlowSlider(
  value: Int, range: IntRange, modifier: Modifier = Modifier, step: Int = 1,
  brush: Brush? = null, accent: Color = LocalWul.current.dawn,
  startIcon: ImageVector? = null, endIcon: ImageVector? = null,
  label: String = "", format: (Int) -> String = { it.toString() },
  onLive: ((Int) -> Unit)? = null, onCommit: (Int) -> Unit,
) {
  val c = LocalWul.current
  val haptic = LocalHapticFeedback.current
  var dragging by remember { mutableStateOf(false) }
  var v by remember(value) { mutableFloatStateOf(value.toFloat()) }
  val live by rememberUpdatedState(onLive)
  val commit by rememberUpdatedState(onCommit)
  val span = (range.last - range.first).coerceAtLeast(1).toFloat()
  fun snap(x: Float) = (((x - range.first) / step).roundToInt() * step + range.first).coerceIn(range.first, range.last)
  val frac by animateFloatAsState(((v - range.first) / span).coerceIn(0f, 1f),
    if (dragging) spring(stiffness = Spring.StiffnessHigh) else spring(Spring.DampingRatioLowBouncy), label = "frac")
  val bubble by animateFloatAsState(if (dragging) 1f else 0f, tween(180), label = "bubble")
  val thumbScale by animateFloatAsState(if (dragging) 1.15f else 1f, spring(Spring.DampingRatioMediumBouncy), label = "thumb")
  val fill = brush ?: Brush.horizontalGradient(listOf(accent.copy(alpha = .75f), accent))
  val shown = snap(v)

  Row(modifier.fillMaxWidth().height(56.dp), verticalAlignment = Alignment.CenterVertically) {
    if (startIcon != null) { Icon(startIcon, null, tint = c.muted, modifier = Modifier.size(20.dp)); Spacer(Modifier.width(10.dp)) }
    BoxWithConstraints(
      Modifier.weight(1f).height(56.dp)
        .semantics {
          contentDescription = label
          stateDescription = format(shown)
          progressBarRangeInfo = ProgressBarRangeInfo(shown.toFloat(), range.first.toFloat()..range.last.toFloat(), (span / step).toInt() - 1)
          setProgress { t -> val n = snap(t); v = n.toFloat(); commit(n); true }
        }
        .pointerInput(range, step) {
          fun at(x: Float) = range.first + (x / size.width).coerceIn(0f, 1f) * span
          detectTapGestures(onPress = { o ->
            dragging = true
            val n = snap(at(o.x)); if (n != snap(v)) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
            v = at(o.x); live?.invoke(n)
            if (tryAwaitRelease()) {          // простое касание; если начался свайп — завершит обработчик перетаскивания
              dragging = false
              v = snap(v).toFloat(); commit(snap(v))
            }
          })
        }
        .pointerInput(range, step) {
          fun at(x: Float) = range.first + (x / size.width).coerceIn(0f, 1f) * span
          detectHorizontalDragGestures(
            onDragStart = { dragging = true },
            onDragEnd = { dragging = false; v = snap(v).toFloat(); commit(snap(v)) },
            onDragCancel = { dragging = false; v = snap(v).toFloat() },
          ) { ch, _ ->
            val before = snap(v)
            v = at(ch.position.x)
            val after = snap(v)
            if (after != before) { haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove); live?.invoke(after) }
          }
        },
      contentAlignment = Alignment.CenterStart,
    ) {
      val w = constraints.maxWidth.toFloat()
      val density = LocalDensity.current
      val thumb = 28.dp
      val thumbPx = with(density) { thumb.toPx() }
      Canvas(Modifier.fillMaxWidth().height(14.dp)) {
        val r = CornerRadius(size.height / 2, size.height / 2)
        drawRoundRect(c.surface2, cornerRadius = r)
        val fw = (size.width * frac).coerceAtLeast(size.height)
        drawRoundRect(fill, size = Size(fw, size.height), cornerRadius = r)
      }
      val x = ((w - thumbPx) * frac)
      Box(
        Modifier.offset { IntOffset(x.roundToInt(), 0) }.size(thumb)
          .graphicsLayer { scaleX = thumbScale; scaleY = thumbScale }
          .shadow(6.dp, CircleShape, ambientColor = accent, spotColor = accent)
          .clip(CircleShape).background(Color.White)
          .border(3.dp, accent, CircleShape),
      )
      // пузырь со значением над бегунком
      Box(
        Modifier.offset { IntOffset((x + thumbPx / 2 - with(density) { 26.dp.toPx() }).roundToInt(), with(density) { (-44).dp.roundToPx() }) }
          .graphicsLayer { alpha = bubble; scaleX = .6f + .4f * bubble; scaleY = .6f + .4f * bubble; transformOrigin = androidx.compose.ui.graphics.TransformOrigin(.5f, 1f) }
          .width(52.dp).clip(RoundedCornerShape(12.dp)).background(accent).padding(vertical = 5.dp),
        contentAlignment = Alignment.Center,
      ) { Text(format(shown), color = c.onAccent, fontSize = 14.sp, fontWeight = FontWeight.Bold, style = Num, maxLines = 1) }
    }
    if (endIcon != null) { Spacer(Modifier.width(10.dp)); Icon(endIcon, null, tint = c.muted, modifier = Modifier.size(22.dp)) }
  }
}

/** Ползунок с заголовком и значением справа. */
@Composable
fun SliderRow(
  title: String, value: Int, range: IntRange, format: (Int) -> String = { it.toString() },
  accent: Color = LocalWul.current.dawn, step: Int = 1, brush: Brush? = null,
  startIcon: ImageVector? = null, endIcon: ImageVector? = null,
  onLive: ((Int) -> Unit)? = null, onCommit: (Int) -> Unit,
) {
  val c = LocalWul.current
  var shown by remember(value) { mutableIntStateOf(value) }
  Column(Modifier.fillMaxWidth().padding(top = 8.dp)) {
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text(title, fontSize = 16.sp, modifier = Modifier.weight(1f), color = c.text)
      AnimatedContent(format(shown), transitionSpec = {
        (slideInVertically { -it / 2 } + fadeIn(tween(150))) togetherWith (slideOutVertically { it / 2 } + fadeOut(tween(120)))
      }, label = "val") { Text(it, color = accent, style = Num, fontSize = 15.sp, fontWeight = FontWeight.SemiBold) }
    }
    GlowSlider(value, range, step = step, brush = brush, accent = accent, startIcon = startIcon, endIcon = endIcon,
      label = title, format = format, onLive = { shown = it; onLive?.invoke(it) }) { shown = it; onCommit(it) }
  }
}

// ---------------------------------------------------------------- выбор

/** Ряд «таблеток» (дни недели, длительность рассвета, оценки) с плавной сменой цвета. */
@Composable
fun Pills(items: List<String>, selected: (Int) -> Boolean, accent: Color = LocalWul.current.dawn, onTap: (Int) -> Unit) {
  val c = LocalWul.current
  Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
    items.forEachIndexed { i, s ->
      val on = selected(i)
      val bg by animateColorAsState(if (on) accent else c.surface2, tween(220), label = "pill")
      val fg by animateColorAsState(if (on) c.onAccent else c.text, tween(220), label = "pillT")
      Box(
        Modifier.weight(1f).height(48.dp).clip(CtlShape).background(bg).bouncy(Role.Checkbox) { onTap(i) },
        contentAlignment = Alignment.Center,
      ) {
        Text(s, color = fg, fontSize = 14.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Medium, maxLines = 1)
      }
    }
  }
}

@Composable
fun Chip(icon: ImageVector?, text: String, tint: Color = LocalWul.current.muted) {
  val c = LocalWul.current
  Row(
    Modifier.clip(RoundedCornerShape(50)).background(tint.copy(alpha = .12f)).padding(horizontal = 10.dp, vertical = 5.dp),
    verticalAlignment = Alignment.CenterVertically,
  ) {
    if (icon != null) { Icon(icon, null, tint = tint, modifier = Modifier.size(15.dp)); Spacer(Modifier.width(5.dp)) }
    Text(text, fontSize = 13.sp, color = if (tint == c.muted) c.muted else c.text, maxLines = 1)
  }
}

@Composable
fun ChipRow(content: @Composable RowScope.() -> Unit) =
  Row(Modifier.padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), content = content)

/** Три «прыгающих» столбика — идёт прослушивание. */
@Composable
fun Equalizer(color: Color, modifier: Modifier = Modifier) {
  val t = rememberInfiniteTransition(label = "eq")
  val h = listOf(0, 160, 320).map { d ->
    t.animateFloat(.25f, 1f, infiniteRepeatable(tween(420, delayMillis = d), RepeatMode.Reverse), label = "b$d")
  }
  Canvas(modifier.size(18.dp)) {
    val bw = size.width / 5
    h.forEachIndexed { i, a ->
      val bh = size.height * a.value
      drawRoundRect(color, Offset(bw * (i * 2), size.height - bh), Size(bw, bh), CornerRadius(bw / 2, bw / 2))
    }
  }
}

/** Список 8 звуков: выбор + прослушивание на часах. */
@Composable
fun SoundList(selected: Int, previewing: Int, accent: Color, onPick: (Int) -> Unit, onPlay: (Int) -> Unit) {
  val c = LocalWul.current
  Column(Modifier.fillMaxWidth().clip(CtlShape).background(c.surface2)) {
    sounds().forEachIndexed { i, name ->
      val on = i == selected
      val bg by animateColorAsState(if (on) accent.copy(alpha = .14f) else Color.Transparent, tween(220), label = "snd")
      Row(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).background(bg).clickable(role = Role.RadioButton) { onPick(i) }.padding(start = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
      ) {
        Box(Modifier.size(22.dp), contentAlignment = Alignment.Center) {
          val k by animateFloatAsState(if (on) 1f else 0f, spring(Spring.DampingRatioMediumBouncy), label = "chk")
          if (previewing == i) Equalizer(accent)
          else if (k > .01f) Icon(Icons.Rounded.CheckCircle, tr("Выбран", "Selected"), tint = accent,
            modifier = Modifier.size(20.dp).graphicsLayer { scaleX = k; scaleY = k; alpha = k.coerceIn(0f, 1f) })
        }
        Spacer(Modifier.width(10.dp))
        Text(name, fontSize = 15.sp, modifier = Modifier.weight(1f), color = if (on) accent else c.text,
          fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal)
        IconButton(onClick = { onPlay(i) }) {
          Icon(if (previewing == i) Icons.Rounded.Stop else Icons.Rounded.PlayArrow,
            if (previewing == i) tr("Остановить", "Stop") + " $name" else tr("Прослушать", "Play") + " $name",
            tint = if (previewing == i) accent else c.muted)
        }
      }
    }
  }
}

/** Индикатор связи: зелёный пульсирует, красный горит ровно. */
@Composable
fun Dot(on: Boolean) {
  val c = LocalWul.current
  val t = rememberInfiniteTransition(label = "dot")
  val k by t.animateFloat(1f, 2.4f, infiniteRepeatable(tween(1600), RepeatMode.Restart), label = "halo")
  Box(Modifier.size(18.dp), contentAlignment = Alignment.Center) {
    if (on) Box(Modifier.size(8.dp).graphicsLayer { scaleX = k; scaleY = k; alpha = (2.4f - k) / 1.4f * .5f }.clip(CircleShape).background(c.ok))
    Box(Modifier.size(8.dp).clip(CircleShape).background(if (on) c.ok else c.danger))
  }
}

@Composable
fun KeyValue(k: String, v: String) {
  Row(Modifier.fillMaxWidth().heightIn(min = 40.dp), verticalAlignment = Alignment.CenterVertically) {
    Text(k, fontSize = 15.sp, modifier = Modifier.weight(1f), color = LocalWul.current.text)
    Muted(v, size = 15)
  }
}

/** Крупная кнопка с градиентом. */
@Composable
fun GradientButton(text: String, brush: Brush, textColor: Color, modifier: Modifier = Modifier, icon: ImageVector? = null,
                   enabled: Boolean = true, onClick: () -> Unit) {
  Row(
    modifier.fillMaxWidth().heightIn(min = 52.dp).graphicsLayer { alpha = if (enabled) 1f else .45f }
      .clip(RoundedCornerShape(16.dp)).background(brush).bouncy(enabled = enabled, onClick = onClick).padding(horizontal = 18.dp),
    verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
  ) {
    if (icon != null) { Icon(icon, null, tint = textColor, modifier = Modifier.size(20.dp)); Spacer(Modifier.width(8.dp)) }
    Text(text, color = textColor, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
  }
}

/** Кнопка-контур (вторичное действие). */
@Composable
fun GhostButton(text: String, color: Color, modifier: Modifier = Modifier, icon: ImageVector? = null, onClick: () -> Unit) {
  Row(
    modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(16.dp))
      .border(1.5.dp, color.copy(alpha = .45f), RoundedCornerShape(16.dp)).bouncy(onClick = onClick).padding(horizontal = 18.dp),
    verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
  ) {
    if (icon != null) { Icon(icon, null, tint = color, modifier = Modifier.size(20.dp)); Spacer(Modifier.width(8.dp)) }
    Text(text, color = color, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
  }
}

/** Время, у которого каждая изменившаяся цифра «перелистывается». */
@Composable
fun FlipText(text: String, style: TextStyle, color: Color) {
  Row {
    text.forEachIndexed { i, ch ->
      AnimatedContent(ch, transitionSpec = {
        (slideInVertically(tween(260)) { -it } + fadeIn(tween(260))) togetherWith (slideOutVertically(tween(260)) { it } + fadeOut(tween(200)))
      }, label = "flip$i") { Text(it.toString(), style = style, color = color) }
    }
  }
}
