// Gameplay actions driven by the generic interaction system: fuel, shop, workshop, dialogue, settings.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace gtabr {

void Game::openPanel(Panel p) {
  p.open = true;
  p.anim = 0;
  panel_ = std::move(p);
  // freeze pedestrians that are being spoken to
  uiRects_.clear();
}

void Game::closePanel() {
  if (!panel_.open) return;
  auto cb = std::move(panel_.onClose);
  panel_.open = false;
  panel_.options.clear();
  for (Npc& n : npcs_)
    if (n.state == NpcState::Talk) { n.state = n.stationary ? NpcState::Work : NpcState::Idle; n.stateTimer = 1.0f; }
  if (cb) cb();
}

float Game::repairCostCents(const Vehicle& v) const {
  float missing = 100.0f - v.health;
  if (missing < 0.5f) return 0;
  return 4500.0f + missing * 260.0f;
}

// ------------------------------------------------------------------------------------------------ fuel
void Game::openFuelPanel(int pumpId) {
  const PumpDef* pump = nullptr;
  for (const PumpDef& p : world_.pumps) if (p.id == pumpId) pump = &p;
  if (!pump) return;
  Vec2 pv{pump->pos.x, pump->pos.z};
  int vi = -1;
  float bd = 5.7f;
  for (size_t i = 0; i < vehicles_.size(); ++i) {
    float d = (vehicles_[i].pos - pv).length();
    if (d < bd) { bd = d; vi = (int)i; }
  }
  Panel p;
  p.title = "Posto Boa Viagem";
  p.portrait = "frentista";
  if (vi < 0) {
    p.text = "Estacione o veículo perto da bomba para abastecer.";
    p.options.push_back({"Entendi", "", nullptr, "", true, true, nullptr});
    openPanel(p);
    return;
  }
  const Vehicle& v = vehicles_[vi];
  const VehicleDef& d = vehicleDef(v.model);
  float room = d.fuelCap - v.fuel;
  float price = fuelPriceCents();
  p.text = std::string("Gasolina comum: ") + fmtMoney((int)price) + " por litro. Escolha o valor:";
  p.vehicleFuel = v.fuel;
  p.vehicleCap = d.fuelCap;
  p.footer = std::string(d.name) + " • " + fmtFloat(v.fuel, 1) + " / " + fmtFloat(d.fuelCap, 0) + " L";
  bool full = room < 0.4f;
  int fillCost = (int)std::ceil(room * price);
  auto add = [&](const std::string& label, const std::string& sub, int cents, bool fillMode) {
    PanelOption o;
    o.label = label;
    o.sub = sub;
    o.icon = "fuel";
    int cost = fillMode ? fillCost : std::min(cents, fillCost);
    o.enabled = !full && moneyCents_ > 0 && (moneyCents_ >= std::min(cost, 100) );
    if (full) o.sub = "Tanque cheio";
    else if (moneyCents_ < cost && !fillMode && moneyCents_ < cents) o.sub += " (saldo insuficiente)";
    o.closes = true;
    int vii = vi, pid = pumpId;
    o.action = [this, vii, pid, cents, fillMode]() { startFueling(vii, pid, fillMode ? 0 : cents); };
    p.options.push_back(o);
  };
  add("R$ 20", "≈ " + fmtFloat(2000.0f / price, 1) + " litros", 2000, false);
  add("R$ 50", "≈ " + fmtFloat(5000.0f / price, 1) + " litros", 5000, false);
  add("Completar tanque", full ? "Tanque cheio" : fmtMoney(fillCost) + " • " + fmtFloat(room, 1) + " L", 0, true);
  p.options.push_back({"Cancelar", "", "close", "", true, true, nullptr});
  openPanel(p);
}

void Game::startFueling(int vi, int pumpId, int amountCents) {
  (void)pumpId;
  Vehicle& v = vehicles_[vi];
  const VehicleDef& d = vehicleDef(v.model);
  float price = fuelPriceCents();
  float room = std::max(0.0f, d.fuelCap - v.fuel);
  float liters = amountCents == 0 ? room : std::min(room, amountCents / price);
  int cost = (int)std::ceil(liters * price);
  if (cost > moneyCents_) {
    liters = std::floor(moneyCents_ / price * 10.0f) / 10.0f;
    cost = (int)std::ceil(liters * price);
  }
  if (liters < 0.05f || cost <= 0) { toast("Saldo insuficiente", "coin", rgba(1.0f, 0.5f, 0.45f)); return; }
  moneyCents_ -= cost;
  moneyShow_ = 4.0f;
  fueling_.active = true;
  fueling_.vehicle = vi;
  fueling_.litersLeft = liters;
  fueling_.total = liters;
  fueling_.rate = 7.0f;
  toast("Abastecendo " + fmtFloat(liters, 1) + " L • -" + fmtMoney(cost), "fuel");
}

// ------------------------------------------------------------------------------------------------ shop
void Game::useItem(int item) {
  if (item <= 0 || item >= kItemCount) return;
  if (inventory_[item] <= 0) { toast("Você não tem esse item", "bag", rgba(1.0f, 0.6f, 0.5f)); return; }
  inventory_[item]--;
  applyItemEffects(itemDef(item));
}

void Game::applyItemEffects(const ItemDef& d) {
  player_.health = std::min(100.0f, player_.health + d.health);
  player_.stamina = std::min(100.0f, player_.stamina + d.stamina);
  if (d.runBoostSecs > 0) player_.runBoost = std::max(player_.runBoost, d.runBoostSecs);
  std::string msg = std::string("Usou: ") + d.name;
  if (d.health > 0) msg += " (+" + fmtFloat(d.health, 0) + " vida)";
  if (d.stamina > 0) msg += " (+" + fmtFloat(d.stamina, 0) + " fôlego)";
  toast(msg, d.icon);
}

void Game::buyItem(int item, bool fromShelf) {
  const ItemDef& d = itemDef(item);
  if (moneyCents_ < d.priceCents) { toast("Dinheiro insuficiente para " + std::string(d.name), "coin", rgba(1.0f, 0.5f, 0.45f)); return; }
  moneyCents_ -= d.priceCents;
  inventory_[item]++;
  moneyShow_ = 4.0f;
  toast(std::string("Comprou: ") + d.name + " • -" + fmtMoney(d.priceCents) + (fromShelf ? "" : ""), "bag");
}

void Game::openShopPanel(const char* portrait) {
  Panel p;
  p.title = "Mercado do Zé";
  p.portrait = portrait;
  p.text = "O que vai levar hoje?";
  for (int i = 1; i < kItemCount; ++i) {
    const ItemDef& d = itemDef(i);
    if (!d.sold) continue;
    PanelOption o;
    o.label = d.name;
    o.sub = fmtMoney(d.priceCents) + (inventory_[i] > 0 ? "  •  você tem " + std::to_string(inventory_[i]) : "");
    o.art = d.art ? d.art : "";
    o.enabled = moneyCents_ >= d.priceCents;
    o.closes = false;
    int item = i;
    o.action = [this, item]() {
      buyItem(item, false);
      // refresh the sub-labels/enabled flags in place
      for (size_t k = 0; k < panel_.options.size(); ++k) {
        int id = (int)k + 1;
        if (id >= kItemCount || panel_.options[k].label == "Fechar") continue;
        const ItemDef& dd = itemDef(id);
        panel_.options[k].enabled = moneyCents_ >= dd.priceCents;
        panel_.options[k].sub = fmtMoney(dd.priceCents) + (inventory_[id] > 0 ? "  •  você tem " + std::to_string(inventory_[id]) : "");
      }
    };
    p.options.push_back(o);
  }
  p.options.push_back({"Fechar", "", "close", "", true, true, nullptr});
  openPanel(p);
}

void Game::openAttendantPanel() {
  Panel p;
  p.title = "Atendente";
  p.portrait = "atendente";
  p.text = "Olá! Bem-vindo ao Mercado do Zé. Posso ajudar?";
  p.options.push_back({"Ver produtos", "Água, lanches, kit de primeiros socorros...", "cart", "", true, false, [this]() { openShopPanel("atendente"); }});
  p.options.back().closes = false;
  p.options.push_back({"Conversar", "", "chat", "", true, false, [this]() {
    Panel q;
    q.title = "Atendente";
    q.portrait = "atendente";
    q.text = "O pão sai quentinho às cinco da tarde. E se precisar de mecânico, a Oficina Silva fica no quarteirão da praça.";
    q.options.push_back({"Valeu!", "", nullptr, "", true, true, nullptr});
    openPanel(q);
  }});
  p.options.back().closes = false;
  p.options.push_back({"Tchau", "", "close", "", true, true, nullptr});
  openPanel(p);
}

// ------------------------------------------------------------------------------------------------ workshop
void Game::repairVehicle(int vi) {
  Vehicle& v = vehicles_[vi];
  int cost = (int)repairCostCents(v);
  if (cost <= 0) { toast("O veículo está perfeito", "wrench"); return; }
  if (cost > moneyCents_) { toast("Dinheiro insuficiente para o reparo", "coin", rgba(1.0f, 0.5f, 0.45f)); return; }
  moneyCents_ -= cost;
  moneyShow_ = 4.0f;
  v.health = 100.0f;
  toast("Reparo concluído • -" + fmtMoney(cost), "wrench");
}

void Game::openWorkshopPanel() {
  // find an owned vehicle inside / near the service bay (prefer the one the player is driving)
  const RectF zone = world_.serviceBay.inflated(3.5f);
  int vi = -1;
  if (player_.vehicle >= 0) vi = player_.vehicle;
  else {
    float bd = 1e9f;
    for (size_t i = 0; i < vehicles_.size(); ++i)
      if (zone.contains(vehicles_[i].pos.x, vehicles_[i].pos.y)) {
        float d = (vehicles_[i].pos - player_.pos).length();
        if (d < bd) { bd = d; vi = (int)i; }
      }
  }
  Panel p;
  p.title = "Oficina Silva";
  p.portrait = "mecanico";
  if (vi < 0 || !zone.contains(vehicles_[vi].pos.x, vehicles_[vi].pos.y)) {
    p.text = "Traz o carro pra dentro da área marcada em frente à oficina que eu dou uma olhada.";
    p.options.push_back({"Beleza", "", nullptr, "", true, true, nullptr});
    openPanel(p);
    return;
  }
  const Vehicle& v = vehicles_[vi];
  const VehicleDef& d = vehicleDef(v.model);
  int cost = (int)repairCostCents(v);
  p.vehicleHealth = v.health;
  p.footer = std::string(d.name) + " • Integridade " + fmtFloat(v.health, 0) + "%";
  if (cost <= 0) {
    p.text = "Esse aí tá novinho. Não precisa de reparo nenhum!";
    p.options.push_back({"Valeu, Silva", "", nullptr, "", true, true, nullptr});
  } else {
    p.text = "Dei uma olhada: dá pra deixar zerado. Reparo completo de lataria e mecânica.";
    PanelOption o;
    o.label = "Reparo básico";
    o.sub = fmtMoney(cost) + " • restaura a integridade para 100%";
    o.icon = "wrench";
    o.enabled = moneyCents_ >= cost;
    if (!o.enabled) o.sub += " (saldo insuficiente)";
    int vii = vi;
    o.action = [this, vii]() { repairVehicle(vii); };
    p.options.push_back(o);
    p.options.push_back({"Agora não", "", "close", "", true, true, nullptr});
  }
  openPanel(p);
}

// ------------------------------------------------------------------------------------------------ dialogue
static const char* kPoiNames[3] = {"Posto Boa Viagem", "Mercado do Zé", "Oficina Silva"};

void Game::openNpcPanel(int idx) {
  if (idx < 0 || idx >= (int)npcs_.size()) return;
  Npc& n = npcs_[idx];
  n.state = NpcState::Talk;
  n.path.clear();
  Panel p;
  auto setWaypoint = [this](int poi) {
    waypoint_.active = true;
    waypoint_.name = kPoiNames[poi];
    waypoint_.pos = poi == 0 ? world_.poiGas : (poi == 1 ? world_.poiMarketDoor : world_.poiWorkshop);
    toast(std::string("Marcador: ") + kPoiNames[poi], "pin");
  };
  auto whereOptions = [&](Panel& pn) {
    for (int poi = 0; poi < 3; ++poi) {
      PanelOption o;
      o.label = std::string("Onde fica o ") + (poi == 0 ? "posto?" : (poi == 1 ? "mercado?" : "mecânico?"));
      o.icon = "pin";
      o.closes = true;
      o.action = [setWaypoint, poi]() { setWaypoint(poi); };
      pn.options.push_back(o);
    }
  };
  switch (n.role) {
    case 4: closePanel(); openAttendantPanel(); return;
    case 1: {
      p.title = "Frentista";
      p.portrait = "frentista";
      p.text = "Boa tarde! Estaciona do lado da bomba e usa o botão de interagir. Gasolina a " + fmtMoney((int)fuelPriceCents()) + " o litro.";
      whereOptions(p);
      p.options.push_back({"Valeu!", "", nullptr, "", true, true, nullptr});
      break;
    }
    case 2: {
      closePanel();
      n.state = NpcState::Talk;
      openWorkshopPanel();
      return;
    }
    case 3: {
      p.title = "Seu Manoel";
      p.portrait = "vizinho";
      p.text = "Esse bairro tá bonito, né? Qualquer coisa me chama.";
      whereOptions(p);
      if (moneyCents_ < 2000 && neighbourCooldown_ <= 0.0f) {
        p.text = "Tá apertado? Toma, é pra você abastecer. Depois me paga um café!";
        PanelOption o;
        o.label = "Aceitar R$ 50,00";
        o.icon = "coin";
        o.action = [this]() { moneyCents_ += 5000; moneyShow_ = 4.0f; neighbourCooldown_ = 120.0f; toast("Seu Manoel te ajudou: +R$ 50,00", "coin"); };
        p.options.insert(p.options.begin(), o);
      }
      p.options.push_back({"Tchau, Seu Manoel", "", nullptr, "", true, true, nullptr});
      break;
    }
    default: {
      p.title = "Morador";
      p.text = chatLine(n);
      n.bubble = "";
      whereOptions(p);
      p.options.push_back({"Tchau", "", nullptr, "", true, true, nullptr});
      break;
    }
  }
  openPanel(p);
}

}  // namespace gtabr
