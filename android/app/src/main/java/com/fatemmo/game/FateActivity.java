package com.fatemmo.game;

import org.libsdl.app.SDLActivity;

public class FateActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[]{ "SDL2", "main" };
    }
}
