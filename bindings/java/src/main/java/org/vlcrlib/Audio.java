package org.vlcrlib;

/** Sound effects and streamed music. */
public final class Audio {

    private Audio() {
    }

    /** Open the audio device; false when the build has no audio backend. */
    public static boolean init() {
        return Native.audioInit() != 0;
    }

    public static void close() {
        Native.audioClose();
    }

    public static boolean available() {
        return Native.audioAvailable() != 0;
    }

    /** A short sound effect. */
    public static final class Sound implements AutoCloseable {

        private int handle;
        private float volume = 1f;

        private Sound(int handle) {
            if (handle <= 0) {
                throw new IllegalArgumentException("invalid sound handle");
            }
            this.handle = handle;
        }

        public static Sound load(String path) {
            int handle = Native.soundLoad(path);
            if (handle <= 0) {
                throw new IllegalStateException("vlcrlib: cannot load sound '" + path + "'");
            }
            return new Sound(handle);
        }

        public int handle() {
            return handle;
        }

        public boolean playing() {
            return Native.soundPlaying(handle) != 0;
        }

        public void play() {
            Native.soundPlay(handle);
        }

        public void stop() {
            Native.soundStop(handle);
        }

        public float volume() {
            return volume;
        }

        public void volume(float value) {
            volume = value;
            Native.soundSetVolume(handle, value);
        }

        @Override
        public void close() {
            if (handle > 0) {
                Native.soundUnload(handle);
                handle = 0;
            }
        }
    }

    /** A streamed track; call {@link Music#update()} every frame. */
    public static final class Music implements AutoCloseable {

        private int handle;
        private float volume = 1f;

        private Music(int handle) {
            if (handle <= 0) {
                throw new IllegalArgumentException("invalid music handle");
            }
            this.handle = handle;
        }

        public static Music load(String path) {
            int handle = Native.musicLoad(path);
            if (handle <= 0) {
                throw new IllegalStateException("vlcrlib: cannot load music '" + path + "'");
            }
            return new Music(handle);
        }

        public int handle() {
            return handle;
        }

        public boolean playing() {
            return Native.musicPlaying(handle) != 0;
        }

        public float length() {
            return Native.musicLength(handle);
        }

        public float played() {
            return Native.musicPlayed(handle);
        }

        public void play() {
            Native.musicPlay(handle);
        }

        public void stop() {
            Native.musicStop(handle);
        }

        public void pause() {
            Native.musicPause(handle);
        }

        public void resume() {
            Native.musicResume(handle);
        }

        public void update() {
            Native.musicUpdate(handle);
        }

        public float volume() {
            return volume;
        }

        public void volume(float value) {
            volume = value;
            Native.musicSetVolume(handle, value);
        }

        @Override
        public void close() {
            if (handle > 0) {
                Native.musicUnload(handle);
                handle = 0;
            }
        }
    }
}
