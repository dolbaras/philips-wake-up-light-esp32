package com.wul.app.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import com.wul.app.data.REGIONS

/** Язык интерфейса: "ru" | "en" — берётся из settings.lang модуля (до подключения — язык системы). */
val LocalLang = staticCompositionLocalOf { "ru" }

/** Строка на текущем языке: tr("Будильник", "Alarm"). */
@Composable
@ReadOnlyComposable
fun tr(ru: String, en: String): String = if (LocalLang.current == "en") en else ru

@Composable
@ReadOnlyComposable
fun isEn() = LocalLang.current == "en"

private val SOUNDS_RU = listOf("Птицы", "Лес", "Горы", "Океан", "Пианино", "Петух", "Йога", "Дзен-сад")
private val SOUNDS_EN = listOf("Birds", "Forest", "Mountain", "Ocean", "Piano", "Rooster", "Yoga", "Zen Garden")
private val DAYS_RU = listOf("Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс")
private val DAYS_EN = listOf("Mo", "Tu", "We", "Th", "Fr", "Sa", "Su")
private val QUALITY_RU = listOf("Плохо", "Так себе", "Нормально", "Хорошо", "Отлично")
private val QUALITY_EN = listOf("Bad", "Meh", "Okay", "Good", "Great")

@Composable @ReadOnlyComposable fun sounds() = if (isEn()) SOUNDS_EN else SOUNDS_RU
@Composable @ReadOnlyComposable fun soundName(i: Int) = sounds().getOrElse(i) { "—" }
@Composable @ReadOnlyComposable fun days() = if (isEn()) DAYS_EN else DAYS_RU
@Composable @ReadOnlyComposable fun quality() = if (isEn()) QUALITY_EN else QUALITY_RU

@Composable
@ReadOnlyComposable
fun daysText(m: Int): String = when (m) {
  0 -> tr("Один раз", "Once")
  127 -> tr("Каждый день", "Every day")
  31 -> tr("По будням", "Weekdays")
  96 -> tr("По выходным", "Weekends")
  else -> days().filterIndexed { i, _ -> (m shr i) and 1 == 1 }.joinToString(", ")
}

/** Длительность: «2 д 3 ч», «1 ч 5 мин», «12 мин». */
@Composable
@ReadOnlyComposable
fun dur(s: Long): String {
  val m = maxOf(0, Math.round(s / 60.0)); val h = m / 60
  return if (isEn()) when { h >= 24 -> "${h / 24} d ${h % 24} h"; h > 0 -> "$h h ${m % 60} min"; else -> "${m % 60} min" }
  else when { h >= 24 -> "${h / 24} д ${h % 24} ч"; h > 0 -> "$h ч ${m % 60} мин"; else -> "${m % 60} мин" }
}

@Composable @ReadOnlyComposable fun minutes(n: Int) = tr("$n мин", "$n min")

@Composable
@ReadOnlyComposable
fun regionName(id: String) = REGIONS.firstOrNull { it.id == id }?.let { if (isEn()) it.en else it.ru } ?: id
