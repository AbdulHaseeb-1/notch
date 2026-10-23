#pragma once

#include <QByteArray>

#include <deque>
#include <vector>

// A software acoustic echo canceller for conversations over speakers.
//
// While the agent talks, the microphone also hears the speakers. The canceller
// learns the path from the speakers to the microphone (an adaptive NLMS filter
// over the last 256 ms) from what the player is playing, and subtracts the
// echo it predicts, so what is left is the room and the user's own voice.
//
// Both streams are 16 kHz, mono, 16-bit. The reference is whatever was handed
// to the sound device; the canceller pops it at the rate the microphone runs, so
// the two stay in step whatever the device's buffering does.
//
// It adapts only while the speakers are playing, and holds still when the
// microphone is much louder than the echo the filter expects (the user talking
// over the agent), so the user's voice is not learned as echo.
class EchoCanceller
{
public:
    static constexpr int kRate = 16000;
    static constexpr int kTaps = 4096;          // 256 ms

    EchoCanceller();

    // Forgets everything learned (a new conversation).
    void reset();
    // The reference still queued will not be heard (the player was flushed).
    void clearReference();
    // What the speakers are about to play.
    void pushReference(const QByteArray &pcm16);
    // Microphone audio in, the same audio with the echo taken out.
    QByteArray process(const QByteArray &mic16);

    // How much echo is being removed, in dB (smoothed; 0 until it has learned something).
    double erleDb() const { return m_erleDb; }
    bool converged() const { return m_erleDb >= 8.0; }
    // True while the speakers are (or just were) playing and the filter is running.
    bool running() const { return m_running; }

private:
    void pushSample(float x);
    float dot() const;

    std::vector<float> m_w;                     // the echo path
    std::vector<float> m_x;                     // reference history, written twice so a window is contiguous
    int m_pos = 0;                              // newest sample (window runs m_pos .. m_pos + kTaps)
    double m_energy = 0.0;                      // sum of squares over the window
    std::deque<float> m_pending;                // reference not yet consumed
    long m_quiet = kTaps;                       // samples since the reference was last non-zero
    long m_farActive = 0;                       // samples of reference seen (how much has been learned from)
    int m_nearBlocks = 0;                       // consecutive blocks judged to hold the user's voice
    double m_pdSm = 0.0;
    double m_peSm = 0.0;
    double m_erleDb = 0.0;
    bool m_running = false;
};
