#pragma once

// Start-with-Windows support via the current user's Run key (no admin rights needed).
bool IsAutostartEnabled();
void SetAutostart(bool enable);

// On the very first launch autostart is switched on. Later launches respect the user's choice.
void ApplyFirstRunAutostart();
