// Jobs, experience and levels: delivery work taken from the shops and the petrol station, paid by distance and punctuality.
// XP comes from jobs and from the first time the player does each thing in town (progress milestones).
#include <algorithm>
#include <cmath>

#include "game.h"
#include "ui_theme.h"

namespace gtabr {

namespace {
const char* kItems[] = {"Pacote de pães", "Marmita quente", "Encomenda de ferramentas", "Envelope de documentos", "Caixa de peças de carro", "Cesta de frutas"};
}

int Game::xpForNext(int level) const { return 100 + 60 * (level - 1); }

void Game::addXp(int amount) {
  if (amount <= 0) return;
  xp_ += amount;
  while (xp_ >= xpForNext(level_)) {
    xp_ -= xpForNext(level_);
    ++level_;
    int off = (int)std::lround(std::min(15.0f, 3.0f * (level_ - 1)));
    toast("Nível " + std::to_string(level_) + "|Descontos nas lojas: " + std::to_string(off) + "%  ·  mais fôlego", "star", theme::kOk);
  }
}

float Game::shopDiscount() const { return std::min(0.15f, 0.03f * (level_ - 1)); }
float Game::staminaPerk() const { return 1.0f + 0.08f * (level_ - 1); }

void Game::markProgress(int bit) {
  if (!(progress_ & (1u << bit))) {
    progress_ |= 1u << bit;
    addXp(30);
  }
}

// Three offers from here (a clerk or the pump attendant): different cargo, different distance, different pay.
void Game::openJobBoard(Vec2 from, const std::string& giver) {
  Panel p;
  p.title = "Bicos de entrega";
  p.role = giver;
  p.text = job_.active ? "Você já tem uma entrega em andamento. Termine ela primeiro." : "Preciso de alguém ágil. Quanto mais longe, mais paga. Chegando no prazo ainda tem bônus.";
  if (job_.active) {
    p.options.push_back({"Abandonar a entrega atual", "", "close", "", true, true, [this]() { cancelJob(); }});
    p.options.push_back({"Voltar", "", nullptr, "", true, true, nullptr});
    openPanel(p);
    return;
  }
  uint32_t base = (uint32_t)(time_ * 13.0f) + (uint32_t)world_.seed;
  for (int i = 0; i < 3; ++i) {
    // pick a destination 70-220 m away
    Vec3 dest{};
    float dist = 0;
    for (int t = 0; t < 30 && !world_.poiList.empty(); ++t) {
      const Vec3& c = world_.poiList[(size_t)(rng_.uni() * world_.poiList.size()) % world_.poiList.size()];
      dist = (Vec2{c.x, c.z} - from).length();
      dest = c;
      if (dist > 70.0f && dist < 220.0f) break;
    }
    if (dist < 20.0f) continue;
    int pay = (int)std::lround((3200.0f + dist * 42.0f) * (1.0f + 0.08f * (level_ - 1)) * (0.9f + 0.2f * rng_.uni()));
    pay = pay / 50 * 50;
    int secs = (int)(50.0f + dist / 1.5f);
    std::string item = kItems[(base + i * 7u) % 6u];
    std::string where = locationName({dest.x, dest.z}, false);
    PanelOption o;
    o.label = item;
    o.sub = fmtMoney(pay) + "  •  " + std::to_string((int)dist) + " m  •  " + std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
    o.icon = "bag";
    o.closes = true;
    o.action = [this, item, where, dest, pay, secs, dist]() {
      job_ = Job();
      job_.active = true; job_.item = item; job_.destName = where; job_.dropoff = dest; job_.pay = pay; job_.timeTotal = (float)secs; job_.timeLeft = (float)secs;
      job_.distance = dist;
      waypoint_.active = true; waypoint_.name = "Entrega: " + where; waypoint_.pos = dest;
      toast("Entrega aceita|" + item + " → " + where, "bag", theme::kAcc);
    };
    p.options.push_back(o);
  }
  p.options.push_back({"Agora não", "", nullptr, "", true, true, nullptr});
  openPanel(p);
}

void Game::cancelJob() {
  if (!job_.active) return;
  job_ = Job();
  waypoint_.active = false;
  toast("Entrega cancelada", "close", theme::kWarn);
}

void Game::completeJob() {
  float frac = job_.timeLeft / std::max(1.0f, job_.timeTotal);
  bool late = job_.timeLeft < 0.0f;
  int pay = late ? job_.pay / 2 : (frac > 0.25f ? (int)(job_.pay * 1.3f) : job_.pay);
  pay = pay / 10 * 10;
  moneyCents_ += pay;
  moneyShow_ = 4.0f;
  ++jobsDone_;
  earned_ += pay;
  int xp = (int)(25.0f + job_.distance / 5.0f) * (late ? 1 : 2) / (late ? 2 : 1);
  std::string how = late ? "com atraso (metade)" : (frac > 0.25f ? "no prazo (+30%)" : "no limite do prazo");
  toast("Entrega concluída|" + fmtMoney(pay) + " " + how, "check", theme::kOk);
  waypoint_.active = false;
  job_ = Job();
  addXp(xp);
}

void Game::updateJobs(float dt) {
  if (!job_.active) return;
  job_.timeLeft -= dt;
  if (job_.timeLeft < -150.0f) { toast("Entrega perdida|O cliente desistiu", "close", theme::kHot); job_ = Job(); waypoint_.active = false; return; }
  if (!job_.warned && job_.timeLeft < 0.0f) { job_.warned = true; toast("Tempo esgotado|Entregue mesmo assim por metade", "bolt", theme::kWarn); }
  // arriving with the cargo
  Vec2 pp = player_.vehicle >= 0 ? vehicles_[player_.vehicle].pos : player_.pos;
  job_.near = (pp - Vec2{job_.dropoff.x, job_.dropoff.z}).length() < 5.5f && !player_.indoors;
}

}  // namespace gtabr
