package com.unravel.player;

import org.libsdl.app.SDLActivity;

/**
 * The player's activity. SDL is linked statically into libmain.so, so that is the only native
 * library to load; SDL then calls its SDL_main.
 */
public class UnravelActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /**
     * The manifest's screenOrientation decides, which the deploy sets from the project. SDL would
     * otherwise pick the orientation from the window's size when the window is created.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
    }
}
