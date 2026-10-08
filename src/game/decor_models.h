#pragma once
#include "model.h"
namespace gtabr {
struct DecorModels {
  std::vector<gfx::ModelHandle> trees, props;
  gfx::MaterialHandle material;
  bool ready=false;
};
void buildDecorModels(gfx::Renderer& renderer, DecorModels& out);
}
