// Item catalogue (consumables and weapons share the radial wheel infrastructure).
#pragma once
#include <string>

namespace gtabr {

enum class ItemCategory { Weapon = 0, Item = 1 };

struct ItemDef {
  int id;
  const char* key;       // stable key used in save files
  const char* name;
  const char* desc;
  int priceCents;
  ItemCategory category;
  const char* art;       // ui_art entry (product photo), may be null
  const char* icon;      // fallback icon
  float health;          // instant health restored
  float stamina;         // instant stamina restored
  float runBoostSecs;    // temporary stamina-free running
  bool sold;             // sold in the market
};

constexpr int kItemCount = 9;
inline const ItemDef& itemDef(int id) {
  static const ItemDef defs[kItemCount] = {
      {0, "punhos", "Punhos", "Sem arma. Resolve no braço.", 0, ItemCategory::Weapon, nullptr, "fist", 0, 0, 0, false},
      {1, "agua", "Água mineral", "Recupera o fôlego.", 350, ItemCategory::Item, "prod_agua", "bolt", 0, 45, 0, true},
      {2, "refri", "Refrigerante lata", "Gelado. Um pouco de energia.", 600, ItemCategory::Item, "prod_refri", "bolt", 4, 25, 0, true},
      {3, "salgadinho", "Salgadinho", "Crocante e salgado.", 500, ItemCategory::Item, "prod_salgadinho", "heart", 12, 5, 0, true},
      {4, "pao", "Pão francês", "Quentinho da padaria.", 250, ItemCategory::Item, "prod_pao", "heart", 20, 0, 0, true},
      {5, "cafe", "Café", "Acorda qualquer um.", 450, ItemCategory::Item, "prod_cafe", "bolt", 0, 30, 25, true},
      {6, "barra", "Barra de cereal", "Energia para o dia.", 400, ItemCategory::Item, "prod_barra", "heart", 14, 12, 0, true},
      {7, "energetico", "Energético", "Corra sem cansar por um tempo.", 990, ItemCategory::Item, "prod_energetico", "bolt", 0, 100, 45, true},
      {8, "kit", "Kit de primeiros socorros", "Recupera bastante vida.", 2490, ItemCategory::Item, "prod_kit", "heart", 65, 0, 0, true},
  };
  return defs[id < 0 || id >= kItemCount ? 0 : id];
}

}  // namespace gtabr
