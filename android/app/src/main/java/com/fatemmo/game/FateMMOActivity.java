package com.fatemmo.game;

import org.libsdl.app.SDLActivity;

public class FateMMOActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"  // libmain.so — our game code
        };
    }
}
