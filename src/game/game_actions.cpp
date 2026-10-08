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
  p.role = "Frentista";
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
  markProgress(kPgFueled);
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

// shop price after the level discount
static int discounted(int cents, float disc) { return (int)std::lround(cents * (1.0f - disc)) / 5 * 5; }

int Game::shopPriceCents(int shopId, int item) const {
  if (shopId >= 0 && shopId < (int)world_.shops.size())
    for (const ShopStock& st : world_.shops[shopId].stock)
      if (st.kind == 0 && st.id == item) return discounted(st.priceCents, shopDiscount());
  return discounted(itemDef(item).priceCents, shopDiscount());
}


void Game::buyItem(int item, bool fromShelf, int priceCents) {
  const ItemDef& d = itemDef(item);
  int price = priceCents >= 0 ? priceCents : d.priceCents;
  if (moneyCents_ < price) { toast("Dinheiro insuficiente para " + std::string(d.name), "coin", rgba(1.0f, 0.5f, 0.45f)); return; }
  moneyCents_ -= price;
  inventory_[item]++;
  moneyShow_ = 4.0f;
  markProgress(kPgBought);
  toast(std::string("Comprou: ") + d.name + " • -" + fmtMoney(price) + (fromShelf ? "" : ""), "bag");
}

bool Game::buyWeapon(int weapon, int priceCents) {
  const WeaponDef& w = weaponDef(weapon);
  if (player_.owned[weapon]) { toast(std::string("Você já tem: ") + w.name, "bag", rgba(1.0f, 0.75f, 0.5f)); return false; }
  if (moneyCents_ < priceCents) { toast("Dinheiro insuficiente para " + std::string(w.name), "coin", rgba(1.0f, 0.5f, 0.45f)); return false; }
  moneyCents_ -= priceCents;
  giveWeapon(weapon, 0);
  moneyShow_ = 4.0f;
  markProgress(kPgBought);
  toast(std::string("Comprou: ") + w.name + " • -" + fmtMoney(priceCents), w.icon);
  return true;
}

void Game::buyStock(int shopId, int idx) {
  if (shopId < 0 || shopId >= (int)world_.shops.size()) return;
  const ShopDef& sh = world_.shops[shopId];
  if (idx < 0 || idx >= (int)sh.stock.size()) return;
  const ShopStock& st = sh.stock[idx];
  if (st.kind == 1) buyWeapon(st.id, st.priceCents);
  else buyItem(st.id, false, st.priceCents);
}

static const char* portraitFor(ShopKind k) {
  switch (k) {
    case ShopKind::Ferragens: return "mecanico";
    case ShopKind::Padaria: return "atendente";
    default: return "atendente";
  }
}

// The shop window: this shop's own stock and prices. Buying deducts the money and puts the item in the inventory (weapons are
// handed over directly); the list refreshes in place so the player can keep shopping.
void Game::openShopPanel(int shopId) {
  if (shopId < 0 || shopId >= (int)world_.shops.size()) return;
  const ShopDef& sh = world_.shops[shopId];
  Panel p;
  p.title = sh.name;
  p.portrait = portraitFor(sh.kind);
  p.text = sh.kind == ShopKind::Ferragens ? "Ferramenta boa dura a vida toda. O que vai levar?" : "O que vai levar hoje?";
  p.footer = "Saldo " + fmtMoney(moneyCents_);
  for (size_t i = 0; i < sh.stock.size(); ++i) {
    const ShopStock& st = sh.stock[i];
    PanelOption o;
    if (st.kind == 1) {
      const WeaponDef& w = weaponDef(st.id);
      o.label = w.name;
      o.icon = w.icon;
      bool owned = player_.owned[st.id];
      o.sub = owned ? "Você já tem" : fmtMoney(st.priceCents);
      o.enabled = !owned && moneyCents_ >= st.priceCents;
    } else {
      const ItemDef& d = itemDef(st.id);
      o.label = d.name;
      o.art = d.art ? d.art : "";
      o.icon = d.icon;
      o.sub = fmtMoney(st.priceCents) + (inventory_[st.id] > 0 ? "  •  você tem " + std::to_string(inventory_[st.id]) : "");
      o.enabled = moneyCents_ >= st.priceCents;
    }
    o.closes = false;
    int sid = shopId, idx = (int)i;
    o.action = [this, sid, idx]() {
      int sc = panel_.scroll;
      buyStock(sid, idx);
      openShopPanel(sid);
      panel_.scroll = sc;
      panel_.anim = 1.0f;
    };
    p.options.push_back(o);
  }
  p.options.push_back({"Fechar", "", "close", "", true, true, nullptr});
  openPanel(p);
}

void Game::openAttendantPanel(int shopId) {
  if (shopId < 0 || shopId >= (int)world_.shops.size()) return;
  const ShopDef& sh = world_.shops[shopId];
  Panel p;
  p.title = sh.kind == ShopKind::Ferragens ? "Seu Joaquim" : (sh.kind == ShopKind::Padaria ? "Padeira" : "Atendente");
  p.portrait = portraitFor(sh.kind);
  p.text = "Olá! Bem-vindo à " + sh.name + ". Posso ajudar?";
  const char* what = sh.kind == ShopKind::Ferragens ? "Facas, bastões, pé de cabra, taco e kit de primeiros socorros"
                     : (sh.kind == ShopKind::Padaria ? "Pão quentinho, café e lanches" : "Água, lanches, café e kit de primeiros socorros");
  int sid = shopId;
  p.options.push_back({"Ver produtos", what, "cart", "", true, false, [this, sid]() { openShopPanel(sid); }});
  p.options.back().closes = false;
  p.options.push_back({"Conversar", "", "chat", "", true, false, [this, sid]() {
    Panel q;
    const ShopDef& s2 = world_.shops[sid];
    q.title = s2.kind == ShopKind::Ferragens ? "Seu Joaquim" : (s2.kind == ShopKind::Padaria ? "Padeira" : "Atendente");
    q.portrait = portraitFor(s2.kind);
    switch (s2.kind) {
      case ShopKind::Ferragens: q.text = "Aqui o freguês leva o que precisa e volta sempre. Se for pra se defender, leva o bastão. Mas pensa duas vezes."; break;
      case ShopKind::Padaria: q.text = "O pão sai quentinho às cinco da tarde. Fica por perto que já já sai uma fornada!"; break;
      case ShopKind::Conveniencia: q.text = "A gente fica aberto a noite toda. Se precisar de combustível, a bomba é logo ali fora."; break;
      default: q.text = "O pão sai quentinho às cinco da tarde. E se precisar de mecânico, a Oficina Silva fica no quarteirão."; break;
    }
    q.options.push_back({"Valeu!", "", nullptr, "", true, true, nullptr});
    openPanel(q);
  }});
  p.options.back().closes = false;
  p.options.push_back({"Tem algum bico?", "Entregas pagas por distância", "bag", "", true, false, [this, sid]() {
    openJobBoard(Vec2{world_.shops[sid].door.x, world_.shops[sid].door.z}, "Atendente");
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
  markProgress(kPgRepaired);
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
  p.role = "Mecânico";
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
    case 4: closePanel(); openAttendantPanel(n.shop); return;
    case 1: {
      p.title = "Frentista";
      p.portrait = "frentista";
      p.role = "Posto";
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
      p.role = "Vizinho";
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
      p.role = "Pedestre";
      // contextual dialogue: armed / wanted player, mood of the person, otherwise small talk with variety
      if (player_.weapon != kWpnFists) {
        p.text = npcLine(n, 4);
        n.fear = std::min(1.0f, n.fear + 0.3f);
      } else if (wanted_ > 0) p.text = npcLine(n, 11);
      else if (n.mood == Mood::Angry) p.text = npcLine(n, 10);
      else p.text = npcLine(n, rng_.chance(0.4f) ? 0 : 1);
      n.bubble = "";
      if (player_.weapon == kWpnFists && wanted_ == 0) {
        PanelOption more;
        more.label = "Puxar mais papo";
        more.icon = "chat";
        more.closes = false;
        more.action = [this, idx]() { panel_.text = npcLine(npcs_[idx], rng_.chance(0.5f) ? 1 : 9); panel_.reveal = 0; };
        p.options.push_back(more);
        whereOptions(p);
      }
      {
        PanelOption push;
        push.label = "Empurrar";
        push.icon = "fist";
        push.action = [this, idx]() {
          Npc& m = npcs_[idx];
          m.knockVel += (m.pos - player_.pos).normalized() * 2.5f;
          emitEvent(EventKind::Assault, m.pos, 8.0f, {ActorKind::Player, 0}, {ActorKind::Npc, idx}, 0.2f);
          if (m.temper > 0.45f || m.bravery > 0.7f) {
            m.state = NpcState::Fight; m.target = {ActorKind::Player, 0}; m.stateTimer = 15.0f;
            npcSay(m, npcLine(m, 10));
          } else {
            npcSay(m, npcLine(m, 2));
            npcStartFlee(m, player_.pos, 6.0f);
          }
        };
        p.options.push_back(push);
      }
      p.options.push_back({"Tchau", "", nullptr, "", true, true, nullptr});
      break;
    }
  }
  openPanel(p);
}

}  // namespace gtabr
