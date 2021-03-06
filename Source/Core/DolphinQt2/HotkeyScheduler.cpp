// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt2/HotkeyScheduler.h"

#include <algorithm>
#include <thread>

#include <QCoreApplication>

#include "AudioCommon/AudioCommon.h"

#include "Common/Thread.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HotkeyManager.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/USB/Bluetooth/BTBase.h"
#include "Core/State.h"

#include "DolphinQt2/MainWindow.h"
#include "DolphinQt2/Settings.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

constexpr const char* DUBOIS_ALGORITHM_SHADER = "dubois";

HotkeyScheduler::HotkeyScheduler() : m_stop_requested(false)
{
  HotkeyManagerEmu::Initialize();
  HotkeyManagerEmu::LoadConfig();
  HotkeyManagerEmu::Enable(true);
}

HotkeyScheduler::~HotkeyScheduler()
{
  Stop();
}

void HotkeyScheduler::Start()
{
  m_stop_requested.Set(false);
  m_thread = std::thread(&HotkeyScheduler::Run, this);
}

void HotkeyScheduler::Stop()
{
  m_stop_requested.Set(true);

  if (m_thread.joinable())
    m_thread.join();
}

static bool IsHotkey(int id, bool held = false)
{
  return HotkeyManagerEmu::IsPressed(id, held);
}

static void HandleFrameskipHotkeys()
{
  constexpr int MAX_FRAME_SKIP_DELAY = 60;
  constexpr int FRAME_STEP_DELAY = 30;

  static int frame_step_count = 0;
  static int frame_step_delay = 1;
  static int frame_step_delay_count = 0;
  static bool frame_step_hold = false;

  if (IsHotkey(HK_FRAME_ADVANCE_INCREASE_SPEED))
  {
    frame_step_delay = std::min(frame_step_delay + 1, MAX_FRAME_SKIP_DELAY);
    return;
  }

  if (IsHotkey(HK_FRAME_ADVANCE_DECREASE_SPEED))
  {
    frame_step_delay = std::max(frame_step_delay - 1, 0);
    return;
  }

  if (IsHotkey(HK_FRAME_ADVANCE_RESET_SPEED))
  {
    frame_step_delay = 1;
    return;
  }

  if (IsHotkey(HK_FRAME_ADVANCE, true))
  {
    if (frame_step_delay_count < frame_step_delay && frame_step_hold)
      frame_step_delay_count++;

    if ((frame_step_count == 0 || frame_step_count == FRAME_STEP_DELAY) && !frame_step_hold)
    {
      Core::DoFrameStep();
      frame_step_hold = true;
    }

    if (frame_step_count < FRAME_STEP_DELAY)
    {
      frame_step_count++;
      frame_step_hold = false;
    }

    if (frame_step_count == FRAME_STEP_DELAY && frame_step_hold &&
        frame_step_delay_count >= frame_step_delay)
    {
      frame_step_hold = false;
      frame_step_delay_count = 0;
    }

    return;
  }
  else if (frame_step_count > 0)
  {
    // Reset frame advance
    frame_step_count = 0;
    frame_step_hold = false;
    frame_step_delay_count = 0;
  }
}

void HotkeyScheduler::Run()
{
  while (!m_stop_requested.IsSet())
  {
    Common::SleepCurrentThread(1000 / 60);

    if (!HotkeyManagerEmu::IsEnabled())
      continue;

    if (Core::GetState() == Core::State::Uninitialized || Core::GetState() == Core::State::Paused)
      g_controller_interface.UpdateInput();

    if (Core::GetState() != Core::State::Stopping)
    {
      HotkeyManagerEmu::GetStatus();

      if (!Core::IsRunningAndStarted())
        continue;

      if (IsHotkey(HK_OPEN))
        emit Open();

      // Disc

      if (IsHotkey(HK_EJECT_DISC))
        emit EjectDisc();

      if (IsHotkey(HK_CHANGE_DISC))
        emit ChangeDisc();

      // Fullscreen
      if (IsHotkey(HK_FULLSCREEN))
      {
        emit FullScreenHotkey();

        // Prevent fullscreen from getting toggled too often
        Common::SleepCurrentThread(100);
      }

      // Refresh Game List
      if (IsHotkey(HK_REFRESH_LIST))
        emit RefreshGameListHotkey();

      // Pause and Unpause
      if (IsHotkey(HK_PLAY_PAUSE))
        emit TogglePauseHotkey();

      // Stop
      if (IsHotkey(HK_STOP))
        emit StopHotkey();

      // Reset
      if (IsHotkey(HK_RESET))
        emit ResetHotkey();

      // Frameskipping
      HandleFrameskipHotkeys();

      // Screenshot
      if (IsHotkey(HK_SCREENSHOT))
        emit ScreenShotHotkey();

      // Exit
      if (IsHotkey(HK_EXIT))
        emit ExitHotkey();

      auto& settings = Settings::Instance();

      // Recording
      if (IsHotkey(HK_START_RECORDING))
        emit StartRecording();

      if (IsHotkey(HK_EXPORT_RECORDING))
        emit ExportRecording();

      if (IsHotkey(HK_READ_ONLY_MODE))
        emit ToggleReadOnlyMode();

      // Wiimote
      if (SConfig::GetInstance().m_bt_passthrough_enabled)
      {
        const auto ios = IOS::HLE::GetIOS();
        auto device = ios ? ios->GetDeviceByName("/dev/usb/oh1/57e/305") : nullptr;

        if (device != nullptr)
          std::static_pointer_cast<IOS::HLE::Device::BluetoothBase>(device)->UpdateSyncButtonState(
              IsHotkey(HK_TRIGGER_SYNC_BUTTON, true));
      }

      if (SConfig::GetInstance().bEnableDebugging)
      {
        CheckDebuggingHotkeys();
      }

      // TODO: HK_MBP_ADD

      if (SConfig::GetInstance().bWii)
      {
        int wiimote_id = -1;
        if (IsHotkey(HK_WIIMOTE1_CONNECT))
          wiimote_id = 0;
        if (IsHotkey(HK_WIIMOTE2_CONNECT))
          wiimote_id = 1;
        if (IsHotkey(HK_WIIMOTE3_CONNECT))
          wiimote_id = 2;
        if (IsHotkey(HK_WIIMOTE4_CONNECT))
          wiimote_id = 3;
        if (IsHotkey(HK_BALANCEBOARD_CONNECT))
          wiimote_id = 4;

        if (wiimote_id > -1)
          emit ConnectWiiRemote(wiimote_id);
      }

      const auto show_msg = [](OSDMessage message) {
        if (g_renderer)
          g_renderer->ShowOSDMessage(message);
      };

      // Volume
      if (IsHotkey(HK_VOLUME_DOWN))
      {
        show_msg(OSDMessage::VolumeChanged);
        settings.DecreaseVolume(3);
      }

      if (IsHotkey(HK_VOLUME_UP))
      {
        show_msg(OSDMessage::VolumeChanged);
        settings.IncreaseVolume(3);
      }

      if (IsHotkey(HK_VOLUME_TOGGLE_MUTE))
      {
        show_msg(OSDMessage::VolumeChanged);
        AudioCommon::ToggleMuteVolume();
      }

      // Graphics
      const auto efb_scale = Config::Get(Config::GFX_EFB_SCALE);

      if (IsHotkey(HK_INCREASE_IR))
      {
        show_msg(OSDMessage::IRChanged);
        Config::SetCurrent(Config::GFX_EFB_SCALE, efb_scale + 1);
      }
      if (IsHotkey(HK_DECREASE_IR))
      {
        show_msg(OSDMessage::IRChanged);
        if (efb_scale > EFB_SCALE_AUTO_INTEGRAL)
          Config::SetCurrent(Config::GFX_EFB_SCALE, efb_scale - 1);
      }

      if (IsHotkey(HK_TOGGLE_CROP))
        Config::SetCurrent(Config::GFX_CROP, !Config::Get(Config::GFX_CROP));

      if (IsHotkey(HK_TOGGLE_AR))
      {
        show_msg(OSDMessage::ARToggled);
        const int aspect_ratio = (static_cast<int>(Config::Get(Config::GFX_ASPECT_RATIO)) + 1) & 3;
        Config::SetCurrent(Config::GFX_ASPECT_RATIO, static_cast<AspectMode>(aspect_ratio));
      }
      if (IsHotkey(HK_TOGGLE_EFBCOPIES))
      {
        show_msg(OSDMessage::EFBCopyToggled);
        Config::SetCurrent(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM,
                           !Config::Get(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM));
      }

      if (IsHotkey(HK_TOGGLE_XFBCOPIES))
      {
        show_msg(OSDMessage::XFBChanged);
        Config::SetCurrent(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM,
                           !Config::Get(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM));
      }
      if (IsHotkey(HK_TOGGLE_IMMEDIATE_XFB))
      {
        show_msg(OSDMessage::XFBChanged);

        Config::SetCurrent(Config::GFX_HACK_IMMEDIATE_XFB,
                           !Config::Get(Config::GFX_HACK_IMMEDIATE_XFB));
      }
      if (IsHotkey(HK_TOGGLE_FOG))
      {
        show_msg(OSDMessage::FogToggled);
        Config::SetCurrent(Config::GFX_DISABLE_FOG, !Config::Get(Config::GFX_DISABLE_FOG));
      }

      if (IsHotkey(HK_TOGGLE_DUMPTEXTURES))
        Config::SetCurrent(Config::GFX_DUMP_TEXTURES, !Config::Get(Config::GFX_DUMP_TEXTURES));

      if (IsHotkey(HK_TOGGLE_TEXTURES))
        Config::SetCurrent(Config::GFX_HIRES_TEXTURES, !Config::Get(Config::GFX_HIRES_TEXTURES));

      Core::SetIsThrottlerTempDisabled(IsHotkey(HK_TOGGLE_THROTTLE, true));

      if (IsHotkey(HK_DECREASE_EMULATION_SPEED))
      {
        show_msg(OSDMessage::SpeedChanged);

        auto speed = SConfig::GetInstance().m_EmulationSpeed - 0.1;
        speed = (speed <= 0 || (speed >= 0.95 && speed <= 1.05)) ? 1.0 : speed;
        SConfig::GetInstance().m_EmulationSpeed = speed;
      }

      if (IsHotkey(HK_INCREASE_EMULATION_SPEED))
      {
        show_msg(OSDMessage::SpeedChanged);

        auto speed = SConfig::GetInstance().m_EmulationSpeed + 0.1;
        speed = (speed >= 0.95 && speed <= 1.05) ? 1.0 : speed;
        SConfig::GetInstance().m_EmulationSpeed = speed;
      }

      // Slot Saving / Loading
      if (IsHotkey(HK_SAVE_STATE_SLOT_SELECTED))
        emit StateSaveSlotHotkey();

      if (IsHotkey(HK_LOAD_STATE_SLOT_SELECTED))
        emit StateLoadSlotHotkey();

      // Stereoscopy
      if (IsHotkey(HK_TOGGLE_STEREO_SBS) || IsHotkey(HK_TOGGLE_STEREO_TAB))
      {
        if (Config::Get(Config::GFX_STEREO_MODE) != StereoMode::SBS)
        {
          // Disable post-processing shader, as stereoscopy itself is currently a shader
          if (Config::Get(Config::GFX_ENHANCE_POST_SHADER) == DUBOIS_ALGORITHM_SHADER)
            Config::SetCurrent(Config::GFX_ENHANCE_POST_SHADER, "");

          Config::SetCurrent(Config::GFX_STEREO_MODE,
                             IsHotkey(HK_TOGGLE_STEREO_SBS) ? StereoMode::SBS : StereoMode::TAB);
        }
        else
        {
          Config::SetCurrent(Config::GFX_STEREO_MODE, StereoMode::Off);
        }
      }

      if (IsHotkey(HK_TOGGLE_STEREO_ANAGLYPH))
      {
        if (Config::Get(Config::GFX_STEREO_MODE) != StereoMode::Anaglyph)
        {
          Config::SetCurrent(Config::GFX_STEREO_MODE, StereoMode::Anaglyph);
          Config::SetCurrent(Config::GFX_ENHANCE_POST_SHADER, DUBOIS_ALGORITHM_SHADER);
        }
        else
        {
          Config::SetCurrent(Config::GFX_STEREO_MODE, StereoMode::Off);
          Config::SetCurrent(Config::GFX_ENHANCE_POST_SHADER, "");
        }
      }

      if (IsHotkey(HK_TOGGLE_STEREO_3DVISION))
      {
        if (Config::Get(Config::GFX_STEREO_MODE) != StereoMode::Nvidia3DVision)
        {
          if (Config::Get(Config::GFX_ENHANCE_POST_SHADER) == DUBOIS_ALGORITHM_SHADER)
            Config::SetCurrent(Config::GFX_ENHANCE_POST_SHADER, "");

          Config::SetCurrent(Config::GFX_STEREO_MODE, StereoMode::Nvidia3DVision);
        }
        else
        {
          Config::SetCurrent(Config::GFX_STEREO_MODE, StereoMode::Off);
        }
      }
    }

    const auto stereo_depth = Config::Get(Config::GFX_STEREO_DEPTH);

    if (IsHotkey(HK_DECREASE_DEPTH, true))
      Config::SetCurrent(Config::GFX_STEREO_DEPTH, std::min(stereo_depth - 1, 0));

    if (IsHotkey(HK_INCREASE_DEPTH, true))
      Config::SetCurrent(Config::GFX_STEREO_DEPTH, std::min(stereo_depth + 1, 100));

    const auto stereo_convergence = Config::Get(Config::GFX_STEREO_CONVERGENCE);

    if (IsHotkey(HK_DECREASE_CONVERGENCE, true))
      Config::SetCurrent(Config::GFX_STEREO_CONVERGENCE, std::max(stereo_convergence - 5, 0));

    if (IsHotkey(HK_INCREASE_CONVERGENCE, true))
      Config::SetCurrent(Config::GFX_STEREO_CONVERGENCE, std::min(stereo_convergence + 5, 500));

    // Freelook
    static float fl_speed = 1.0;

    if (IsHotkey(HK_FREELOOK_DECREASE_SPEED, true))
      fl_speed /= 1.1f;

    if (IsHotkey(HK_FREELOOK_INCREASE_SPEED, true))
      fl_speed *= 1.1f;

    if (IsHotkey(HK_FREELOOK_RESET_SPEED, true))
      fl_speed = 1.0;

    if (IsHotkey(HK_FREELOOK_UP, true))
      VertexShaderManager::TranslateView(0.0, 0.0, -fl_speed);

    if (IsHotkey(HK_FREELOOK_DOWN, true))
      VertexShaderManager::TranslateView(0.0, 0.0, fl_speed);

    if (IsHotkey(HK_FREELOOK_LEFT, true))
      VertexShaderManager::TranslateView(fl_speed, 0.0);

    if (IsHotkey(HK_FREELOOK_RIGHT, true))
      VertexShaderManager::TranslateView(-fl_speed, 0.0);

    if (IsHotkey(HK_FREELOOK_ZOOM_IN, true))
      VertexShaderManager::TranslateView(0.0, fl_speed);

    if (IsHotkey(HK_FREELOOK_ZOOM_OUT, true))
      VertexShaderManager::TranslateView(0.0, -fl_speed);

    if (IsHotkey(HK_FREELOOK_RESET, true))
      VertexShaderManager::ResetView();

    // Savestates
    for (u32 i = 0; i < State::NUM_STATES; i++)
    {
      if (IsHotkey(HK_LOAD_STATE_SLOT_1 + i))
        emit StateLoadSlot(i + 1);

      if (IsHotkey(HK_SAVE_STATE_SLOT_1 + i))
        emit StateSaveSlot(i + 1);

      if (IsHotkey(HK_LOAD_LAST_STATE_1 + i))
        emit StateLoadLastSaved(i + 1);

      if (IsHotkey(HK_SELECT_STATE_SLOT_1 + i))
        emit SetStateSlotHotkey(i + 1);
    }

    if (IsHotkey(HK_SAVE_FIRST_STATE))
      emit StateSaveOldest();

    if (IsHotkey(HK_UNDO_LOAD_STATE))
      emit StateLoadUndo();

    if (IsHotkey(HK_UNDO_SAVE_STATE))
      emit StateSaveUndo();
  }
}

void HotkeyScheduler::CheckDebuggingHotkeys()
{
  if (IsHotkey(HK_STEP))
    emit Step();

  if (IsHotkey(HK_STEP_OVER))
    emit StepOver();

  if (IsHotkey(HK_STEP_OUT))
    emit StepOut();

  if (IsHotkey(HK_SKIP))
    emit Skip();

  if (IsHotkey(HK_SHOW_PC))
    emit ShowPC();

  if (IsHotkey(HK_SET_PC))
    emit Skip();

  if (IsHotkey(HK_BP_TOGGLE))
    emit ToggleBreakpoint();

  if (IsHotkey(HK_BP_ADD))
    emit AddBreakpoint();
}
