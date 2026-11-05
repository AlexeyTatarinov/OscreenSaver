#pragma once

#include <string>

// Sound played when a screenshot is saved or copied.
// The numbers are kept in the settings, so new sounds get new numbers at the end.
enum class SaveSound : int { Shutter = 0, Click = 1, Chime = 2, File = 3, Glass = 4, Birds = 5, Count };

// Plays the sound chosen in the settings (in the background, does not wait).
void PlaySaveSound();

// Plays one sound, e.g. when it is picked in the settings window.
void PlaySaveSound(SaveSound sound, const std::wstring& file);
