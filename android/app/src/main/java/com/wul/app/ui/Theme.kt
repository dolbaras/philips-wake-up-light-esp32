package com.wul.app.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import com.wul.app.ThemePref

/** Токены из design-system/wake-up-light/MASTER.md. */
@Immutable
data class WulColors(
  val bg: Color, val surface: Color, val surface2: Color, val border: Color,
  val text: Color, val muted: Color,
  val dawn: Color, val dawn2: Color, val onDawn: Color,
  val dusk: Color, val duskA: Color, val duskB: Color,
  val ok: Color, val danger: Color, val scrim: Color, val dark: Boolean,
) {
  val dawnBrush get() = Brush.linearGradient(listOf(dawn, dawn2))
  val duskBrush get() = Brush.linearGradient(listOf(duskA, duskB))
}

val DarkWul = WulColors(
  bg = Color(0xFF0B1120), surface = Color(0xFF151D31), surface2 = Color(0xFF1E2840), border = Color(0x14FFFFFF),
  text = Color(0xFFF8FAFC), muted = Color(0xFF94A3B8),
  dawn = Color(0xFFF59E0B), dawn2 = Color(0xFFF97362), onDawn = Color(0xFF1A1205),
  dusk = Color(0xFFA78BFA), duskA = Color(0xFF7C3AED), duskB = Color(0xFF312E81),
  ok = Color(0xFF34D399), danger = Color(0xFFF87171), scrim = Color(0xB8030712), dark = true,
)

val LightWul = WulColors(
  bg = Color(0xFFFFFBEB), surface = Color(0xFFFFFFFF), surface2 = Color(0xFFFDF3DC), border = Color(0xFFF1E3C6),
  text = Color(0xFF1C1917), muted = Color(0xFF57534E),
  dawn = Color(0xFFB45309), dawn2 = Color(0xFFC2410C), onDawn = Color(0xFFFFFFFF),
  dusk = Color(0xFF6D28D9), duskA = Color(0xFF7C3AED), duskB = Color(0xFF312E81),
  ok = Color(0xFF047857), danger = Color(0xFFDC2626), scrim = Color(0x731C1917), dark = false,
)

val LocalWul = staticCompositionLocalOf { DarkWul }

@Composable
fun WulTheme(pref: ThemePref, content: @Composable () -> Unit) {
  val dark = when (pref) { ThemePref.Dark -> true; ThemePref.Light -> false; ThemePref.Auto -> isSystemInDarkTheme() }
  val c = if (dark) DarkWul else LightWul
  val scheme = if (dark) darkColorScheme(
    primary = c.dawn, onPrimary = c.onDawn, secondary = c.dusk, background = c.bg, onBackground = c.text,
    surface = c.surface, onSurface = c.text, surfaceVariant = c.surface2, onSurfaceVariant = c.muted,
    surfaceContainerHigh = c.surface, surfaceContainer = c.surface, outline = c.border, error = c.danger,
  ) else lightColorScheme(
    primary = c.dawn, onPrimary = c.onDawn, secondary = c.dusk, background = c.bg, onBackground = c.text,
    surface = c.surface, onSurface = c.text, surfaceVariant = c.surface2, onSurfaceVariant = c.muted,
    surfaceContainerHigh = c.surface, surfaceContainer = c.surface, outline = c.border, error = c.danger,
  )
  CompositionLocalProvider(LocalWul provides c) {
    MaterialTheme(colorScheme = scheme, content = content)
  }
}
