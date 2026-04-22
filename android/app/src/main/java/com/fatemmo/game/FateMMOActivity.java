package com.fatemmo.game;

import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class FateMMOActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"  // libmain.so — our game code
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Hand the Activity to AdBridge so the native SDK has a Context to
        // attach to.  Must happen BEFORE any JNI call to AdService::initialize.
        AdBridge.attachActivity(this);
    }
}
