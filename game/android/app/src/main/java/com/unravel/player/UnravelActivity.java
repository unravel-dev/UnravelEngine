package com.unravel.player;

import android.content.pm.ApplicationInfo;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The player's activity. SDL is linked statically into libmain.so, the library SDL calls SDL_main in.
 */
public class UnravelActivity extends SDLActivity {
    /**
     * The start intent's extra with the game's command line options, separated by whitespace. The
     * editor's Deploy and Run sets it from Run Arguments, as does
     * "adb shell am start -n <application id>/com.unravel.player.UnravelActivity --es arguments '--gfx-stats'".
     * Only debug packages read it: the activity is exported, so any app on the device can set the extra.
     */
    private static final String ARGUMENTS_EXTRA = "arguments";

    /**
     * Debug packages (the deploy's packages without Release Build) let a four-finger tap switch the
     * graphics statistics on and off. Store packages are not debuggable, so players never see it.
     */
    private static final String STATS_GESTURE_OPTION = "--gfx-stats-gesture";

    /**
     * The .NET runtime's cryptography library keeps the Java VM that loads it, and reaches Android's
     * hashing, certificates and TLS through it. Only a load from Java hands it that VM, so it is loaded
     * here before the runtime opens it; the game's library comes last, as SDL takes the last for the
     * one with SDL_main.
     */
    private static final String DOTNET_CRYPTOGRAPHY_LIBRARY = "System.Security.Cryptography.Native.Android";

    @Override
    protected String[] getLibraries() {
        return new String[] { DOTNET_CRYPTOGRAPHY_LIBRARY, "main" };
    }

    @Override
    protected String[] getArguments() {
        List<String> options = new ArrayList<>();
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return options.toArray(new String[0]);
        }
        String arguments = getIntent() != null ? getIntent().getStringExtra(ARGUMENTS_EXTRA) : null;
        if (arguments != null && !arguments.trim().isEmpty()) {
            options.addAll(Arrays.asList(arguments.trim().split("\\s+")));
        }
        options.add(STATS_GESTURE_OPTION);
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
