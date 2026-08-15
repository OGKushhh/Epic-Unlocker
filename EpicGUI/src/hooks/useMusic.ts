/**
 * useMusic — manages background music playback.
 *
 * Ships a single audio file (MP3 preferred — better compression than WAV
 * for music, ~10x smaller at same perceptual quality). The file is placed
 * in the Tauri app's public/ directory so it's bundled into the app.
 *
 * Controls: play/stop toggle, volume (0–1), mute toggle.
 * All state is React-driven — no global audio singleton needed.
 */

import { useCallback, useEffect, useRef, useState } from "react";

// Path to the shipped music file (relative to the webview's origin).
// In Tauri, public/ assets are served at the root, so this works
// both in dev (npm run dev) and production (tauri build).
const MUSIC_SRC = "/bgm.mp3";

export function useMusic() {
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const [playing, setPlaying] = useState(false);
  const [volume, setVolume] = useState(0.3); // Default 30% — background music should be subtle
  const [muted, setMuted] = useState(false);

  // Create the Audio element once
  useEffect(() => {
    const audio = new Audio(MUSIC_SRC);
    audio.loop = true;
    audio.volume = 0.3;
    audio.preload = "auto";
    audioRef.current = audio;

    return () => {
      audio.pause();
      audio.src = "";
      audioRef.current = null;
    };
  }, []);

  // Sync volume to audio element
  useEffect(() => {
    if (audioRef.current) {
      audioRef.current.volume = muted ? 0 : volume;
    }
  }, [volume, muted]);

  const togglePlay = useCallback(() => {
    const audio = audioRef.current;
    if (!audio) return;

    if (playing) {
      audio.pause();
      setPlaying(false);
    } else {
      audio.play().catch(() => {
        // Autoplay blocked — user needs to interact first.
        // This is fine; they clicked the button, so it should work.
        // But if the audio file is missing, this will also fail.
        console.warn("[Music] Playback failed — audio file may be missing.");
      });
      setPlaying(true);
    }
  }, [playing]);

  const toggleMute = useCallback(() => {
    setMuted((m) => !m);
  }, []);

  return { playing, volume, muted, togglePlay, setVolume, toggleMute };
}
