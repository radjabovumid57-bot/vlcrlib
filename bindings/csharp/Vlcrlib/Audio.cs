using System;

namespace Vlcrlib
{
    /// <summary>Audio device control.</summary>
    public static class Audio
    {
        /// <summary>Open the audio device; returns false when no backend is available.</summary>
        public static bool Init() => Native.audioInit() != 0;

        public static void Close() => Native.audioClose();

        public static bool Available => Native.audioAvailable() != 0;
    }

    /// <summary>A short sound effect.</summary>
    public sealed class Sound : IDisposable
    {
        private int _handle;

        private Sound(int handle)
        {
            if (handle <= 0) throw new ArgumentException("invalid sound handle", nameof(handle));
            _handle = handle;
        }

        public static Sound Load(string path)
        {
            int handle = Native.soundLoad(path);
            if (handle <= 0) throw new InvalidOperationException($"vlcrlib: cannot load sound '{path}'");
            return new Sound(handle);
        }

        public int Handle => _handle;
        public bool Playing => Native.soundPlaying(_handle) != 0;

        public float Volume
        {
            get => _volume;
            set { _volume = value; Native.soundSetVolume(_handle, value); }
        }

        private float _volume = 1f;

        public void Play() => Native.soundPlay(_handle);
        public void Stop() => Native.soundStop(_handle);

        public void Dispose()
        {
            if (_handle <= 0) return;
            Native.soundUnload(_handle);
            _handle = 0;
        }
    }

    /// <summary>A streamed music track; call <see cref="Update"/> every frame.</summary>
    public sealed class Music : IDisposable
    {
        private int _handle;
        private float _volume = 1f;

        private Music(int handle)
        {
            if (handle <= 0) throw new ArgumentException("invalid music handle", nameof(handle));
            _handle = handle;
        }

        public static Music Load(string path)
        {
            int handle = Native.musicLoad(path);
            if (handle <= 0) throw new InvalidOperationException($"vlcrlib: cannot load music '{path}'");
            return new Music(handle);
        }

        public int Handle => _handle;
        public bool Playing => Native.musicPlaying(_handle) != 0;
        public float Length => Native.musicLength(_handle);
        public float Played => Native.musicPlayed(_handle);

        public float Volume
        {
            get => _volume;
            set { _volume = value; Native.musicSetVolume(_handle, value); }
        }

        public void Play() => Native.musicPlay(_handle);
        public void Stop() => Native.musicStop(_handle);
        public void Pause() => Native.musicPause(_handle);
        public void Resume() => Native.musicResume(_handle);
        public void Update() => Native.musicUpdate(_handle);

        public void Dispose()
        {
            if (_handle <= 0) return;
            Native.musicUnload(_handle);
            _handle = 0;
        }
    }
}
