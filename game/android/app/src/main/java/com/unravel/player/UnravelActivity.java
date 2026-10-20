package com.unravel.player;

import org.libsdl.app.SDLActivity;

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

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        String arguments = getIntent() != null ? getIntent().getStringExtra(ARGUMENTS_EXTRA) : null;
        if (arguments == null || arguments.trim().isEmpty()) {
            return new String[0];
        }
        return arguments.trim().split("\\s+");
    }

    /**
     * The manifest's screenOrientation decides, which the deploy sets from the project. SDL would
     * otherwise pick the orientation from the window's size when the window is created.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
    }
}
