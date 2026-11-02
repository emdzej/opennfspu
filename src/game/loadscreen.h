/* The race's loading screen (LoadingScreen 0x4a4850): the era's art with "loading" in the options'
   language and a bar growing by step, drawn in a 640 x 480 window of its own and put centred on the
   display with a flip at each step of the race's load (GameMain's 0 after the race's set-up, then
   GameStartup's and its callees' 1 .. 9; 10 ends it). Step 5 draws the track's art over the era's track
   picture (outside the mission races). fe.txt's NOLOADINGSCREEN (0x657440) turns it off. */
#pragma once

void LoadingScreen(int step);
