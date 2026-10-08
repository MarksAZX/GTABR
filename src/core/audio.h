// Small software mixer with procedurally synthesised effects (no sample files): gunshots, melee impacts, swings,
// reloads, sirens, crashes. Positional (distance attenuation + stereo pan relative to a listener).
// Output: AAudio on Android; on desktop/headless the mixer runs without a device (events are still counted).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "math.h"

namespace gtabr {

class Audio {
 public:
  bool init();
  void shutdown();
  // Fire-and-forget sound at a world position. vol 0..1, pitch multiplier.
  void play(const std::string& id, Vec3 pos, float vol = 1.0f, float pitch = 1.0f);
  // Looping sound handle (sirens, engine). Returns a handle; update position/volume each frame; stop when done.
  int loopStart(const std::string& id, Vec3 pos, float vol = 1.0f);
  void loopUpdate(int handle, Vec3 pos, float vol, float pitch = 1.0f);
  void loopStop(int handle);
  void setListener(Vec3 pos, Vec3 right) { std::lock_guard<std::mutex> l(m_); listener_ = pos; listenerRight_ = right; }
  void setMasterVolume(float v) { master_ = v; }
  // Bus levels: one-shot effects vs looping ambience (sirens, surf, rain). 0 mutes.
  void setBusVolumes(float sfx, float ambience) { sfx_ = sfx; amb_ = ambience; }
  // Renders interleaved stereo float frames (called by the device callback).
  void mix(float* out, int frames);
  int playedCount(const std::string& id) const;
  bool hasDevice() const { return device_ != nullptr; }

 private:
  struct Sample { std::vector<float> data; bool loop = false; };
  struct Voice { const Sample* s = nullptr; double pos = 0; float pitch = 1, vol = 1; Vec3 p; bool loop = false; int handle = 0; bool active = false; };
  void synthesize();
  std::unordered_map<std::string, Sample> samples_;
  std::unordered_map<std::string, int> counts_;
  std::vector<Voice> voices_;
  mutable std::mutex m_;
  Vec3 listener_, listenerRight_{1, 0, 0};
  std::atomic<float> master_{0.9f};
  std::atomic<float> sfx_{1.0f}, amb_{1.0f};
  int nextHandle_ = 1;
  void* device_ = nullptr;
  int rate_ = 48000;
};

}  // namespace gtabr
