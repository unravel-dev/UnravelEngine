package com.unravel.player;

import android.content.pm.ApplicationInfo;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The player's activity. SDL is linked statically into libmain.so, so that is the only native
 * library to load; SDL then calls its SDL_main.
 */
public class UnravelActivity extends SDLActivity {
    /**
     * The start intent's extra with the game's command line options, separated by whitespace. The
     * editor's Deploy and Run sets it from Run Arguments, as does
     * "adb shell am start -n <application id>/com.unravel.player.UnravelActivity --es arguments '--gfx-stats'".
     */
    private static final String ARGUMENTS_EXTRA = "arguments";

    /**
     * Debug packages (the deploy's packages without Release Build) let a four-finger tap switch the
     * graphics statistics on and off. Store packages are not debuggable, so players never see it.
     */
    private static final String STATS_GESTURE_OPTION = "--gfx-stats-gesture";

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        List<String> options = new ArrayList<>();
        String arguments = getIntent() != null ? getIntent().getStringExtra(ARGUMENTS_EXTRA) : null;
        if (arguments != null && !arguments.trim().isEmpty()) {
            options.addAll(Arrays.asList(arguments.trim().split("\\s+")));
        }
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            options.add(STATS_GESTURE_OPTION);
        }
        return options.toArray(new String[0]);
    }

    /**
     * The manifest's screenOrientation decides, which the deploy sets from the project. SDL would
     * otherwise pick the orientation from the window's size when the window is created.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
    }
}
