package com.wul.app

import android.graphics.Color
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.CompositionLocalProvider
import com.wul.app.ui.App
import com.wul.app.ui.LocalLang
import com.wul.app.ui.WulTheme

class MainActivity : ComponentActivity() {
  private val vm: MainViewModel by viewModels()

  override fun onCreate(savedInstanceState: Bundle?) {
    enableEdgeToEdge(SystemBarStyle.dark(Color.TRANSPARENT))
    super.onCreate(savedInstanceState)
    setContent {
      val theme by vm.theme.collectAsStateWithLifecycle()
      val sysDark = isSystemInDarkTheme()
      val dark = theme == ThemePref.Dark || (theme == ThemePref.Auto && sysDark)
      LaunchedEffect(dark) {
        // светлая тема — тёмные значки в строке состояния
        val style = if (dark) SystemBarStyle.dark(Color.TRANSPARENT) else SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT)
        enableEdgeToEdge(style, style)
      }
      val lang by vm.lang.collectAsStateWithLifecycle()
      WulTheme(theme) {
        CompositionLocalProvider(LocalLang provides lang) { App(vm) }
      }
    }
  }

  override fun onResume() {
    super.onResume()
    vm.refreshNow()
  }
}
