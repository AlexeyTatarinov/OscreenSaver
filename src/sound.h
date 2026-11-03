#pragma once

#include <string>

// Sound played when a screenshot is saved or copied.
enum class SaveSound : int { Shutter = 0, Click = 1, Chime = 2, File = 3, Count };

// Plays the sound chosen in the settings (in the background, does not wait).
void PlaySaveSound();

// Plays one sound, e.g. when it is picked in the settings window.
void PlaySaveSound(SaveSound sound, const std::wstring& file);
