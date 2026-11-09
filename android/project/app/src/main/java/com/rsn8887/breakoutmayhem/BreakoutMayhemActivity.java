package com.rsn8887.breakoutmayhem;

import org.libsdl.app.SDLActivity;

public class BreakoutMayhemActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SDL2_ttf is linked statically into libmain.so
        return new String[] { "SDL2", "main" };
    }
}
