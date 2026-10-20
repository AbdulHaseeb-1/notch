#pragma once

#include <QAudioFormat>
#include <QByteArray>

// Audio helpers for push-to-talk. Pure functions on byte buffers, so the
// parts of voice input that can go wrong (odd microphone formats, silence,
// the WAV header) are unit tested without a microphone.
//
// Speech goes to the transcriber as 16 kHz, mono, 16-bit PCM: small (32 KB a
// second) and what speech models are trained on.
namespace voice {

constexpr int kSampleRate = 16000;
constexpr int kBytesPerSecond = kSampleRate * 2;

// Any capture format Qt hands out (8/16/32-bit integer or float, any channel
// count and rate) to mono 16-bit 16 kHz. Channels are averaged; the rate is
// converted by linear interpolation with a short low-pass when it is cut down.
// Returns an empty buffer for a format it cannot read.
QByteArray toMono16k(const QByteArray &raw, const QAudioFormat &format);

// A complete .wav file around 16-bit mono PCM.
QByteArray encodeWav(const QByteArray &pcm16, int sampleRate = kSampleRate);

// 0..1 loudness of a 16-bit mono buffer (root mean square, scaled so ordinary
// speech sits well above 0.1). Empty = 0.
double level(const QByteArray &pcm16);
double peak(const QByteArray &pcm16);

// Seconds of audio in a 16 kHz mono buffer.
double seconds(const QByteArray &pcm16);

// True when the clip is too quiet to hold speech (a muted or unplugged
// microphone): not worth sending anywhere.
bool isSilent(const QByteArray &pcm16);

// Drops silence from both ends, keeping a little (0.15 s) either side so
// words are not clipped. Returns the input unchanged when it is all silence.
QByteArray trimSilence(const QByteArray &pcm16);

// Prepares raw microphone audio for the recognizer: removes the DC offset
// (sound cards bias the samples) and lifts quiet clips to a speaking level.
// On-device speech models are trained on normalized audio; without this a
// low-gain microphone transcribes as silence or garbage. Leaves loud clips
// and pure silence untouched.
QByteArray conditionForStt(const QByteArray &pcm16);

// Repairs the common mistakes a small on-device recognizer makes, before a
// transcript becomes a command: sentence-cases the text, ends it with
// punctuation, collapses stuttered words, and fixes the multi-word
// technology names recognizers mangle ("power shell" -> "PowerShell").
// Conservative by design: only exact word-boundary matches are touched,
// and nothing semantic (word choice, homophones) is ever "corrected".
QString fixTranscript(const QString &text);

} // namespace voice
